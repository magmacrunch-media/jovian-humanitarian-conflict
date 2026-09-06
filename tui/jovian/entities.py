"""Contacts, shots, particles — and the IFF rules.

Ported from ``web/js/entities.js``, minus everything below its drawing line.

**Why identification is built the way it is.** The premise only works if
refusing to shoot is a decision rather than a gamble. At spawn depth a convoy
hull is under 6px wide, so silhouette cannot carry the read, and colour alone
excludes anyone who cannot separate amber from magenta. So a contact announces
itself on four channels that come legible in this order:

1. **Transponder blink** — every depth, including the spawn frame, drawn at a
   constant screen size so it does not shrink away. Motion, not colour. This is
   the channel the fairness test is written against, and the only one that
   survives a character grid untouched.
2. **HUD contact strip** — from :meth:`Entities.list_contacts`, so a cluttered
   frame never hides the answer.
3. **Silhouette** — from ``Z_SHAPE_READABLE`` inward.
4. **Colour** — last, and never alone.

Everything here stores **world** coordinates and converts at draw and collision
time. Storing screen coordinates and hand-scrolling them would tie the entities
to one particular camera drift and tear them off the rail the moment it
changed — and in a terminal the screen mapping is different again, so this is
what lets the rules come across untouched.

**One deliberate divergence from the web build.** Its ``explode`` and
``addPopup`` take a hex colour, because on a canvas the caller knows the
palette. Here a particle carries the ``kind`` it came from and a popup carries
a ``tone``, leaving the palette to the theme. Colour is presentation, this
module is simulation, and the terminal version has to be able to render both
without one — see the fourth channel above.
"""

from __future__ import annotations

import math
from collections.abc import Callable
from dataclasses import dataclass, field
from typing import NamedTuple

from jovian import config, projection

#: Weave rate for a contact's lateral drift. A literal in ``entities.js`` and
#: kept as one, so the two files stay comparable line for line.
DRIFT_RATE = 0.04

#: How hard an aggressive hostile slides toward its target, as a share of its
#: ordinary drift.
AGGRO_SLIDE = 0.55

#: Lateral distance at which an aggressive hostile can lock a convoy.
LOCK_RANGE = 28

#: Half-depth of a hull, for the shot sweep. At ``SHOT_SPEED`` 26 a shot skips
#: 26 units a frame, which is wider than a hull, so the sweep tests the span
#: covered rather than the point it ended on — a point test lets shots pass
#: through contacts at some frame rates and not others.
HULL_Z = 10

#: How long a score popup lives, in frames.
POPUP_LIFE = 46


def _sign(x: float) -> float:
    """``Math.sign``: -1, 0 or 1. Python has no builtin for it."""
    return (x > 0) - (x < 0)


class HitBox(NamedTuple):
    half_w: float
    half_h: float


class Event(NamedTuple):
    """Something the scoring layer needs to know about.

    Events rather than callbacks so the simulation stays free of scoring policy
    and stays testable — the web build's reason, and it matters more here
    because two front ends now consume them.
    """

    type: str
    contact: Contact


@dataclass
class Contact:
    """A hostile or an aid convoy, in world space."""

    id: int
    kind: str
    x: float
    y: float
    z: float
    #: Seeded per contact so a wave does not weave in lockstep.
    phase: float
    drift_seed: float
    #: Set on a hostile that will break off to attack a convoy.
    aggro: bool = False
    #: Set when a hostile locks this convoy; counts down to its loss.
    doom_timer: float = 0.0
    locked_by: int = 0
    dead: bool = False
    #: Set once the convoy has announced itself, so that fires exactly once.
    pinged: bool = False
    #: Frames alive, which is what the telegraph guarantee is measured in.
    age: float = 0.0


@dataclass
class Shot:
    x: float
    y: float
    z: float = 0.0


@dataclass
class Particle:
    x: float
    y: float
    z: float
    vx: float
    vy: float
    life: float
    max_life: float
    #: The kind it came from, not a colour. The theme decides how that reads.
    kind: str


@dataclass
class Popup:
    x: float
    y: float
    z: float
    text: str
    tone: str
    life: float = POPUP_LIFE
    max_life: float = POPUP_LIFE


@dataclass
class Entities:
    contacts: list[Contact] = field(default_factory=list)
    shots: list[Shot] = field(default_factory=list)
    particles: list[Particle] = field(default_factory=list)
    popups: list[Popup] = field(default_factory=list)
    spawn_timer: float = 0.0
    next_id: int = 1
    waves_spawned: int = 0
    events: list[Event] = field(default_factory=list)

    def reset(self) -> None:
        self.contacts.clear()
        self.shots.clear()
        self.particles.clear()
        self.popups.clear()
        self.spawn_timer = 0.0
        self.next_id = 1
        self.waves_spawned = 0
        self.events.clear()

    # ── Spawning ────────────────────────────────────────────────────

    def spread_x(self, n: int, random: Callable[[], float]) -> list[float]:
        """``n`` lateral positions, every pair at least the minimum apart.

        Rejection sampling was the obvious way and it does not work: placing
        three contacts 62 units apart across a 300-unit rail fails often enough
        that a bounded retry loop regularly gave up and stacked a convoy on a
        hostile — precisely the frame where telling them apart has to be
        possible.

        This cannot fail. Reserve the gaps first, draw ``n`` uniform values
        from the slack that remains, sort them, and hand the reserved gap back
        as you go. Uniform over every legal arrangement, no retries, and the
        minimum separation is a property of the construction rather than a hope.
        """
        width = config.SPAWN_X_RANGE * 2
        # Should a wave ever be large enough that the full gaps cannot fit,
        # share the rail out evenly rather than break the rule.
        gap = min(config.SPAWN_MIN_SEPARATION,
                  width / (n - 1) if n > 1 else width)
        slack = max(0.0, width - (n - 1) * gap)

        u = sorted(random() * slack for _ in range(n))
        return [-config.SPAWN_X_RANGE + v + i * gap for i, v in enumerate(u)]

    def spawn_wave(self, t: float, random: Callable[[], float]) -> None:
        """Release a wave."""
        # The opening wave is scripted to show one of each, so it needs two
        # slots regardless of what the curve would have chosen.
        n = 2 if self.waves_spawned == 0 else config.wave_size(t, random)
        if n <= 0:
            return

        xs = self.spread_x(n, random)
        aid_share = config.aid_share(t)
        kinds = ["aid" if random() < aid_share else "hostile" for _ in range(n)]

        if self.waves_spawned == 0:
            # One of each, always. Left to the dice at a 30% convoy share and
            # two per wave, the first convoy took about fifteen seconds to turn
            # up — so the game opened by teaching that everything in the sky is
            # a target, and only then introduced the one rule that contradicts
            # it. Showing both side by side, with the separation rule
            # guaranteeing a gap, is the whole tutorial this game needs.
            kinds[0] = "hostile"
            kinds[1] = "aid"
        else:
            # A wave with nothing to shoot, on a screen with nothing to shoot,
            # leaves the player holding fire at empty sky — which reads as the
            # game having stalled rather than as restraint being asked of them.
            none_hostile = not any(
                c.kind == "hostile" and not c.dead for c in self.contacts
            )
            if none_hostile and "hostile" not in kinds:
                kinds[n - 1] = "hostile"
        self.waves_spawned += 1

        # spread_x returns ascending positions, so pairing them with kinds
        # directly would put convoys on the left of every wave. Shuffle first,
        # with the same Fisher-Yates the web build uses so a scripted sequence
        # of randoms produces the same arrangement on both sides.
        for i in range(len(xs) - 1, 0, -1):
            j = int(random() * (i + 1))
            xs[i], xs[j] = xs[j], xs[i]

        for i in range(n):
            is_aid = kinds[i] == "aid"
            self.contacts.append(Contact(
                id=self.next_id,
                kind="aid" if is_aid else "hostile",
                x=xs[i],
                y=(random() * 2 - 1) * config.CONTACT_Y_SPREAD,
                z=float(config.Z_FAR),
                phase=random() * math.pi * 2,
                drift_seed=0.6 + random() * 0.8,
                aggro=(not is_aid) and random() < config.aggro(t),
            ))
            self.next_id += 1

    # ── Per-frame update ────────────────────────────────────────────

    def update(self, player, rail_speed: float, t: float, dt: float,
               random: Callable[[], float]) -> None:
        self.spawn_timer -= dt
        if self.spawn_timer <= 0:
            self.spawn_wave(t, random)
            self.spawn_timer = config.spawn_interval(t)

        self.update_contacts(player, rail_speed, dt)
        self.update_shots(dt)
        self.update_particles(dt)
        self.update_popups(dt)

    def update_contacts(self, player, rail_speed: float, dt: float) -> None:
        for c in self.contacts:
            c.age += dt
            c.z -= rail_speed * dt
            c.phase += DRIFT_RATE * dt * c.drift_seed

            drift = config.AID_DRIFT if c.kind == "aid" else config.HOSTILE_DRIFT
            c.x += math.cos(c.phase) * drift * dt

            # A convoy announces itself once, on the way in. The simulation
            # only reports that the contact crossed the line; what that becomes
            # is the front end's business, which is what keeps this audio-free
            # and portable to a terminal that has none.
            if c.kind == "aid" and not c.pinged and c.z <= config.Z_PING:
                c.pinged = True
                self.events.append(Event("aid-sighted", c))

            # An aggressive hostile slides toward the nearest convoy rather
            # than weaving, which is what makes escorting an active job: the
            # convoy is not merely something you refrain from shooting, it is
            # something being shot at.
            if c.kind == "hostile" and c.aggro:
                target = self.nearest_aid(c)
                if target is not None:
                    slide = config.HOSTILE_DRIFT * AGGRO_SLIDE
                    c.x += _sign(target.x - c.x) * slide * dt
                    if (abs(target.x - c.x) < LOCK_RANGE
                            and target.doom_timer <= 0
                            and abs(target.z - c.z) < config.LOCK_MAX_DZ):
                        target.doom_timer = config.AID_KILL_FRAMES
                        target.locked_by = c.id
                        self.events.append(Event("aid-locked", target))

            # A convoy under fire dies unless its attacker does first. Clearing
            # the lock when the attacker is gone is what makes the rescue land.
            if c.kind == "aid" and c.doom_timer > 0:
                attacker = next(
                    (h for h in self.contacts if h.id == c.locked_by and not h.dead),
                    None,
                )
                if attacker is None:
                    c.doom_timer = 0.0
                    c.locked_by = 0
                else:
                    c.doom_timer -= dt
                    if c.doom_timer <= 0:
                        c.dead = True
                        self.explode(c)
                        self.events.append(Event("aid-lost", c))

            # Collision with the player, in the z = 0 plane. Only hostiles ram;
            # flying through a convoy is not a punishable act.
            if (not c.dead and c.kind == "hostile" and -14 < c.z < 14
                    and player.can_be_hit()
                    and abs(c.x - player.x) < (config.HOSTILE_W + config.SHIP_W) / 2
                    and abs(c.y - player.y) < (config.HOSTILE_H + config.SHIP_H) / 2):
                c.dead = True
                self.explode(c)
                self.events.append(Event("player-hit", c))

        # Retire what has passed the camera. A convoy that makes it out the far
        # side alive is an escort earned.
        kept: list[Contact] = []
        for c in self.contacts:
            if c.dead:
                continue
            if c.z <= config.Z_NEAR:
                if c.kind == "aid":
                    self.events.append(Event("aid-escorted", c))
                continue
            kept.append(c)
        self.contacts = kept

    def nearest_aid(self, from_: Contact) -> Contact | None:
        best: Contact | None = None
        best_d = math.inf
        for c in self.contacts:
            if c.kind != "aid" or c.dead:
                continue
            d = abs(c.z - from_.z)
            if d < best_d:
                best_d, best = d, c
        return best

    # ── Shots ───────────────────────────────────────────────────────

    def fire(self, player) -> None:
        m = player.muzzle()
        self.shots.append(Shot(m.x, m.y, 0.0))

    def update_shots(self, dt: float) -> None:
        """Advance shots and resolve hits.

        The test is done in world space at the shot's own depth, not on screen,
        so a target centred under the reticle is centred at every distance.
        Doing it on screen would make near targets easier to hit purely because
        their sprites are bigger, which would quietly punish holding fire —
        exactly backwards for this game.
        """
        kept: list[Shot] = []
        for s in self.shots:
            from_z = s.z
            s.z += config.SHOT_SPEED * dt
            if from_z > config.Z_FIRE_MAX:
                continue

            hit: Contact | None = None
            for c in self.contacts:
                if c.dead:
                    continue
                if c.z < from_z - HULL_Z or c.z > s.z + HULL_Z:
                    continue
                box = self.hit_box(c)
                if not projection.in_box(s.x, s.y, c.x, c.y, box.half_w, box.half_h):
                    continue
                # Take the NEAREST candidate, not the first in the list. List
                # order is spawn order, so without this a convoy that happened
                # to spawn earlier would absorb a shot aimed at a hostile in
                # front of it — friendly fire decided by allocation order,
                # which is both unfair and untestable.
                if hit is None or c.z < hit.z:
                    hit = c

            if hit is not None:
                hit.dead = True
                self.explode(hit)
                # Attributed to the player, not to the hostiles. The penalty is
                # severe enough that getting this wrong would be the cruellest
                # bug in the game, so it is asserted by test.
                self.events.append(Event(
                    "friendly-fire" if hit.kind == "aid" else "hostile-killed", hit,
                ))
                continue
            kept.append(s)
        self.shots = kept

        # Dead contacts are removed here as well as in update_contacts, so a
        # kill cannot be scored twice by a second shot arriving the same frame.
        self.contacts = [c for c in self.contacts if not c.dead]

    def hit_box(self, c: Contact) -> HitBox:
        """The box a shot must pass through to hit this contact.

        One place, so collision and anything that wants to draw or assert it
        cannot disagree. The margins favour the player on both sides: generous
        on hostiles, honest on convoys.
        """
        is_aid = c.kind == "aid"
        return HitBox(
            half_w=(config.AID_W if is_aid else config.HOSTILE_W) / 2
            + (config.AID_HIT_MARGIN_W if is_aid else config.HOSTILE_HIT_MARGIN_W),
            half_h=(config.AID_H if is_aid else config.HOSTILE_H) / 2
            + (config.AID_HIT_MARGIN_H if is_aid else config.HOSTILE_HIT_MARGIN_H),
        )

    # ── Effects ─────────────────────────────────────────────────────

    def explode(self, c: Contact) -> None:
        for i in range(config.PARTICLE_COUNT):
            a = (math.pi * 2 * i) / config.PARTICLE_COUNT
            self.particles.append(Particle(
                x=c.x, y=c.y, z=c.z,
                vx=math.cos(a) * config.PARTICLE_SPEED,
                vy=math.sin(a) * config.PARTICLE_SPEED,
                life=float(config.PARTICLE_LIFE),
                max_life=float(config.PARTICLE_LIFE),
                kind=c.kind,
            ))

    def update_particles(self, dt: float) -> None:
        kept: list[Particle] = []
        for p in self.particles:
            p.x += p.vx * dt
            p.y += p.vy * dt
            p.life -= dt
            if p.life > 0:
                kept.append(p)
        self.particles = kept

    def add_popup(self, x: float, y: float, z: float, text: str,
                  tone: str = "neutral") -> None:
        self.popups.append(Popup(x=x, y=y, z=z, text=text, tone=tone))

    def update_popups(self, dt: float) -> None:
        kept: list[Popup] = []
        for p in self.popups:
            p.y -= 0.5 * dt
            p.life -= dt
            if p.life > 0:
                kept.append(p)
        self.popups = kept

    # ── Identification ──────────────────────────────────────────────

    def beacon_lit(self, c: Contact, frame: float) -> bool:
        """Is this contact's transponder lit on the given frame?

        A double-tap rather than a single pulse, because one blink at 2Hz is
        easy to mistake for a rendering artefact among moving sprites, and two
        is not. Hostiles never light — the absence is the signal, which costs
        no pixels and cannot be confused with a dim convoy.

        This is the whole of the channel that makes a terminal port possible:
        no scale, no colour, no sub-cell position. One cell, on or off.
        """
        if c.kind != "aid":
            return False
        p = frame % config.BLINK_PERIOD
        return p < config.BLINK_ON_1 or (config.BLINK_GAP <= p < config.BLINK_ON_2)

    def list_contacts(self) -> list[Contact]:
        """Contacts for the HUD strip, nearest last so near ticks draw over far."""
        return sorted((c for c in self.contacts if not c.dead),
                      key=lambda c: c.z, reverse=True)

    def drain_events(self) -> list[Event]:
        events = self.events
        self.events = []
        return events


__all__ = [
    "AGGRO_SLIDE", "DRIFT_RATE", "HULL_Z", "LOCK_RANGE", "POPUP_LIFE",
    "Contact", "Entities", "Event", "HitBox", "Particle", "Popup", "Shot",
]
