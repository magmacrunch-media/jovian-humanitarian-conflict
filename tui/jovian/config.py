"""The web build's 480x270 canvas, and the only coordinate space the rules know.

Every constant here is transcribed from ``web/js/config.js``, which is the
source of truth for rules and tuning. **The terminal's own size is deliberately
not named in this module**, the same rule Moonlight Drift's ``drift.config``
follows: the moment world units and character cells have constants in the same
file they get mixed, and the bug that produces is a game that plays differently
at two window sizes.

Running these numbers straight onto a character grid would not be a port, it
would be a different game. Physics, identification and scoring stay in world
space and stay byte-identical to the browser; only drawing goes through the
projection, and the cell mapping is a second step layered on top of the
pseudo-3D one in :mod:`jovian.projection`.

**Per-frame convention, inherited from the web build.** Every rate here is
expressed in units per 60fps frame and is multiplied by ``dt`` at the point of
use, so the simulation runs identically at 30fps in a terminal and at 144Hz in
a browser. Anything measured in frames — a cooldown, a telegraph window — is
likewise a count of 60fps frames and is decremented by ``dt``, not by 1.
"""

from __future__ import annotations

CANVAS_W = 480
CANVAS_H = 270

# ── The rail ────────────────────────────────────────────────────────
#
# World space is (x, y, z): x right, y down, z into the screen. z = 0 is the
# plane the player's ship sits on; contacts spawn at Z_FAR and travel toward
# the camera. Screen scale is FOCAL / (z + FOCAL) — exactly 1.0 at z = 0,
# falling off hyperbolically.

#: The one number that decides whether this game is legible.
#:
#: It was 220 first, which is the dramatic choice: scale runs 1.0 to 0.167
#: across the rail, so contacts rush at you six-fold. Measured against the real
#: projection that put every contact in a sixteen-pixel band at the horizon,
#: three to eight pixels wide — fatal in a game whose only question is what you
#: are looking at. 520 trades some of that rush for a frame you can play in:
#:
#:     z=0    1.00        z=600 (firing range)  0.46
#:     z=410  0.56        z=1100 (spawn)        0.32
FOCAL = 520
#: Spawn depth.
Z_FAR = 1100
#: Past the camera; contacts are retired here.
Z_NEAR = -60
#: Screen y the vanishing point sits at.
HORIZON_Y = 118

#: Rail speed ramps with difficulty and then holds. The cap is not a taste
#: decision — it is what keeps the identification budget above the telegraph
#: budget. See :func:`identify_frames`.
RAIL_SPEED_MIN = 4.2
RAIL_SPEED_MAX = 6.0

#: Rail units to full difficulty. At about 5 units a frame this is roughly
#: 1m45s, near enough one play of the track.
DIFFICULTY_DISTANCE = 32000

#: How far the vanishing point slides against the ship. Banking left swings the
#: world right, which is most of what sells the depth.
PARALLAX = 0.35
#: Fraction of the gap the camera closes per frame.
CAM_LAG = 0.08

# ── Player ──────────────────────────────────────────────────────────
#
# The ship flies in the z = 0 plane, so its world x/y are screen offsets from
# the vanishing point and need no projection.

#: Half-width of the box the ship may occupy.
SHIP_X_RANGE = 168

#: Contacts fly in a band around y = 0, so the ship's box is centred a little
#: below that rather than well under it. The first tuning rested the ship at
#: y = 40 with a -34..96 box, which put it below everything it was meant to
#: shoot: an aim-bot flown against it drifted to y = -24 and stayed there,
#: which is the tell that the resting position was wrong.
SHIP_Y_MIN = -70
SHIP_Y_MAX = 84
SHIP_Y_START = 10
SHIP_ACCEL = 0.85
#: Per-frame velocity multiplier, so it is raised to ``dt`` rather than
#: multiplied by it.
SHIP_DRAG = 0.86
SHIP_SPEED_MAX = 5.4
SHIP_W = 26
SHIP_H = 14
#: ``|bank|`` at full lateral speed, which drives the sprite roll.
BANK_MAX = 1

MAX_LIVES = 3
INVINCIBLE_FRAMES = 100

# ── Guns ────────────────────────────────────────────────────────────

SHOT_COOLDOWN = 9
#: z units per frame, away from the camera.
SHOT_SPEED = 26

# Hit boxes are a box per sprite, not one radius for everything, and the
# margins deliberately favour the player in both directions: a hostile is
# easier to hit than it looks, a convoy is exactly as big as it looks.
#
# The first cut had this backwards — one circular radius, 11 for a hostile and
# 15 for a convoy — so the thing you were aiming at was the smaller target and
# the thing that costs you the run was the larger one. Every near miss punished
# you twice: the hostile survived and the convoy behind it took the shot.
HOSTILE_HIT_MARGIN_W = 7
HOSTILE_HIT_MARGIN_H = 6
AID_HIT_MARGIN_W = 0
AID_HIT_MARGIN_H = 0

#: Shots do nothing beyond this depth. It sits well inside the window where
#: every identification channel is already legible, so there is no such thing
#: as a shot you were not given the information to hold.
Z_FIRE_MAX = 600

# ── Contacts ────────────────────────────────────────────────────────

HOSTILE_W = 22
HOSTILE_H = 16
AID_W = 34
AID_H = 20

#: Half-height of the band contacts spawn into. Wide enough that they use the
#: frame vertically rather than filing along the horizon, and inside the ship's
#: own Y box so everything that spawns can be reached.
CONTACT_Y_SPREAD = 52

#: Lateral drift, in world units per frame, while closing.
HOSTILE_DRIFT = 0.9
AID_DRIFT = 0.32

# ── The transponder ─────────────────────────────────────────────────
#
# Aid convoys squawk a steady double-blink; hostiles are dark. Drawn at a
# constant screen size at every depth, so this channel is readable from the
# frame a contact spawns — unlike silhouette or colour.
#
# This is the channel that makes a terminal port possible at all: it owes
# nothing to resolution, to sub-cell position, or to hue.

#: The full cycle in 60fps frames. 30 = 2Hz.
BLINK_PERIOD = 30
#: Frames 0..6 lit.
BLINK_ON_1 = 6
#: And 10..14 lit — a double-tap, not a plain pulse.
BLINK_ON_2 = 14
BLINK_GAP = 10

#: Fairness budget. A convoy must complete two full squawk cycles before it can
#: possibly be shot, plus a human reaction allowance. :data:`Z_FAR` and
#: :data:`Z_FIRE_MAX` are sized from this rather than the other way round, and
#: :func:`identify_frames` is what asserts they still agree.
TELEGRAPH_MIN_FRAMES = 60
REACTION_FRAMES = 18

#: Depth at which a convoy's transponder is heard as well as seen. Well outside
#: :data:`Z_FIRE_MAX` on purpose: the point of the sound is to say a convoy is
#: inbound while you still have every option, including the option to stop
#: shooting.
#:
#: A terminal has no audio, so this channel is lost in the port. It is kept
#: here because the number still marks where the game considers a contact
#: announced, and a terminal may want to spend a different channel there.
Z_PING = 900

#: Silhouettes become readable around here; colour is the last channel and
#: never the only one.
Z_SHAPE_READABLE = 410

# ── Spawning ────────────────────────────────────────────────────────
# EARLY/LATE pairs are interpolated by difficulty.

SPAWN_INTERVAL_EARLY = 84
SPAWN_INTERVAL_LATE = 32

#: Two from the very first wave, not one. A wave of one is either a hostile or
#: a convoy, never both, and the difference between them is the only thing this
#: game asks you to learn — so the opening shows them side by side.
WAVE_SIZE_EARLY = 2
WAVE_SIZE_LATE = 3

#: Share of spawned contacts that are aid convoys. Deliberately flat: the moral
#: pressure must not thin out as the shooting gets busier.
AID_SHARE_EARLY = 0.30
AID_SHARE_LATE = 0.26

#: How readily hostiles break off to attack a convoy rather than the player.
HOSTILE_AGGRO_EARLY = 0.25
HOSTILE_AGGRO_LATE = 0.75

#: A hostile only locks a convoy it is actually near in depth. Without this a
#: hostile at the far end of the rail could mark a convoy at the near end, and
#: the line drawn between them crossed the whole screen pointing at something
#: the player could not yet reach.
LOCK_MAX_DZ = 260

#: A convoy under fire dies this many frames after a hostile locks it, which is
#: the window you have to kill the attacker. Bounded from both sides:
#:
#: **Floor.** Worst case the attacker is at the opposite corner of the rail: 64
#: frames to fly there, 23 for the shot to reach maximum firing depth, 18 of
#: reaction and up to 9 of gun cooldown — 114 frames before a perfect player
#: can land it. The first tuning was 105, at which the convoy was already dead.
#: Not a tight window, an impossible one.
#:
#: **Ceiling.** A convoy has a median 125 frames of rail left when locked, so a
#: timer much past that means it escapes before the clock runs out and the
#: threat stops existing. Share of locked convoys that die, measured over a
#: 150-second passive run: 105 -> 52%, 135 -> 42%, 150 -> 36%, 165 -> 4%,
#: 180 -> 1%. The collapse between 150 and 165 is the timer overtaking the
#: rail. 150 is the largest value that still leaves the rule teeth.
AID_KILL_FRAMES = 150

#: Minimum lateral separation between two contacts spawned in one wave, so a
#: convoy is never hidden behind a hostile at the moment you must identify it.
SPAWN_MIN_SEPARATION = 62
SPAWN_X_RANGE = 150

#: The cloud deck's bands recycle over their own depth span rather than
#: :data:`Z_FAR`. Tied to Z_FAR they stopped short of the horizon and left the
#: far third of the deck a flat wall of colour.
DECK_Z_SPAN = 3200

# ── Scoring ─────────────────────────────────────────────────────────

SCORE_HOSTILE = 100
SCORE_ESCORT = 500
SCORE_FRIENDLY_FIRE = -1000
COMBO_MAX = 6
#: Frames before a streak lapses.
COMBO_WINDOW = 180

#: Three friendly-fire hits end the run outright. Restraint is not optional
#: scoring advice — it is a losing condition, which is the whole point of the
#: title.
MAX_STRIKES = 3

# ── Effects ─────────────────────────────────────────────────────────

PARTICLE_COUNT = 10
PARTICLE_LIFE = 24
PARTICLE_SPEED = 2.6


# ── The difficulty curve ────────────────────────────────────────────
#
# Plain functions rather than a class: they are pure, they hold no state, and
# the web build's `Difficulty` object is only an object because JavaScript has
# no module-level functions to reach for.


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def difficulty_at(distance: float) -> float:
    """0 at the start, 1 once :data:`DIFFICULTY_DISTANCE` is behind you."""
    return min(1.0, max(0.0, distance / DIFFICULTY_DISTANCE))


def rail_speed(t: float) -> float:
    return lerp(RAIL_SPEED_MIN, RAIL_SPEED_MAX, t)


def spawn_interval(t: float) -> float:
    return lerp(SPAWN_INTERVAL_EARLY, SPAWN_INTERVAL_LATE, t)


def aid_share(t: float) -> float:
    return lerp(AID_SHARE_EARLY, AID_SHARE_LATE, t)


def aggro(t: float) -> float:
    return lerp(HOSTILE_AGGRO_EARLY, HOSTILE_AGGRO_LATE, t)


def wave_size(t: float, rand) -> int:
    """Contacts released together. The fractional part is a per-wave coin flip.

    ``rand`` is required rather than defaulting to :mod:`random`, so a run is
    reproducible from a seed and a test never depends on global state. The web
    build defaults it because a browser has no seeded run to protect.
    """
    raw = lerp(WAVE_SIZE_EARLY, WAVE_SIZE_LATE, t)
    base = int(raw)
    return base + (1 if rand() < raw - base else 0)


def identify_frames(t: float) -> float:
    """Frames a contact spends between spawning and entering firing range.

    This is the identification budget, and the reason :data:`RAIL_SPEED_MAX` is
    capped where it is. It must stay at or above
    ``TELEGRAPH_MIN_FRAMES + REACTION_FRAMES`` at *every* difficulty, which is
    only in doubt at ``t = 1`` because the rail is fastest there.

    The whole premise of the game rests on this one inequality: refusing to
    shoot is a decision rather than a gamble only if a convoy has squawked long
    enough to be identified before it can possibly be shot.
    """
    return (Z_FAR - Z_FIRE_MAX) / rail_speed(t)
