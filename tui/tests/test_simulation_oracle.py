"""The ported simulation, flown against the JavaScript that ships.

`test_oracle.py` compares the constants and the transform. This compares the
things that move: the ship's flight, wave spawning, the transponder, and two
whole runs stepped frame by frame with every contact, shot and event checked.

Randomness is not reimplemented. The oracle generates its sequence with a
seeded mulberry32, uses it, and emits the array; the helpers here replay it.
Identical input is a property of the transport rather than of two PRNGs
happening to agree — 32-bit wraparound and ``Math.imul`` being exactly the kind
of thing that differs subtly between languages.
"""

from __future__ import annotations

import json
import math
import pathlib
import shutil
import subprocess

import pytest

from jovian import config
from jovian.entities import Contact, Entities
from jovian.player import Player

ORACLE = pathlib.Path(__file__).resolve().parent.parent / "tools" / "js_oracle.mjs"
REL = 1e-9


@pytest.fixture(scope="module")
def js():
    if shutil.which("node") is None:
        pytest.skip("needs node to run the shipped JS")
    out = subprocess.run(["node", str(ORACLE)], capture_output=True, text=True)
    if out.returncode != 0:
        pytest.fail(f"oracle failed:\n{out.stderr}")
    return json.loads(out.stdout)


def scripted(randoms):
    """The oracle's ``scripted()``: a fresh cursor over the shared array.

    A new one per scenario, exactly as the JS side does, because where the
    cursor starts changes what spawns.
    """
    i = 0

    def draw():
        nonlocal i
        v = randoms[i % len(randoms)]
        i += 1
        return v

    return draw


# ── The ship ────────────────────────────────────────────────────────

FLIGHT = [
    (-1, 0, False, 40), (0, 0, False, 20), (1, 0, False, 60),
    (0, -1, False, 40), (0, 1, False, 90), (1, 1, True, 30),
    (0, 0, True, 25), (-1, -1, False, 50), (0, 0, False, 15),
]


def test_the_ship_flies_the_same_way(js):
    """Three timesteps over the same scripted stick.

    dt is varied because drag is a per-frame multiplier raised to dt, which is
    the one place a port is likely to write ``* dt`` and pass every 60Hz test.
    """
    for dt, want_frames in js["player"]:
        p = Player()
        got = []
        for ax, ay, fire, count in FLIGHT:
            for _ in range(count):
                fired = p.update(ax, ay, fire, dt)
                got.append([p.x, p.y, p.vx, p.vy, p.bank, p.shoot_cooldown, fired])

        assert len(got) == len(want_frames)
        for i, (mine, theirs) in enumerate(zip(got, want_frames, strict=True)):
            for field, a, b in zip(
                ("x", "y", "vx", "vy", "bank", "cooldown", "fired"), mine, theirs
            , strict=True):
                if field == "fired":
                    assert a == b, f"dt={dt} frame {i}: fired {a} != {b}"
                else:
                    assert a == pytest.approx(b, rel=REL), f"dt={dt} frame {i}: {field}"


# ── Spawning ────────────────────────────────────────────────────────


def test_wave_sizes_agree(js):
    for t, want in js["waveSize"]:
        r = scripted(js["randoms"])
        assert [config.wave_size(t, r) for _ in want] == want


def test_spawn_positions_agree(js):
    """Including n=20, which is past the point the reserved gaps fit and the
    rail is shared out evenly instead."""
    for n, want in js["spreadX"]:
        got = Entities().spread_x(n, scripted(js["randoms"]))
        assert got == pytest.approx(want, rel=REL), f"n={n}"


def test_spawned_waves_agree(js):
    """Kinds, positions, drift seeds and aggro flags, over four waves.

    The first wave is scripted to one of each and the rest are not, so this
    covers both branches, plus the shuffle that stops convoys always landing on
    the left.
    """
    for t, want_waves in js["spawn"]:
        e = Entities()
        r = scripted(js["randoms"])
        for w, want in enumerate(want_waves):
            e.spawn_wave(t, r)
            got = [[c.id, c.kind, c.x, c.y, c.z, c.phase, c.drift_seed, c.aggro]
                   for c in e.contacts]
            assert len(got) == len(want), f"t={t} wave {w}: count"
            for mine, theirs in zip(got, want, strict=True):
                assert mine[0] == theirs[0]
                assert mine[1] == theirs[1], f"t={t} wave {w}: kind"
                assert mine[2:7] == pytest.approx(theirs[2:7], rel=REL)
                assert mine[7] == theirs[7], f"t={t} wave {w}: aggro"


# ── Identification ──────────────────────────────────────────────────


def test_the_transponder_agrees(js):
    """Ninety frames of it, for both kinds. The double-tap either matches on
    every frame or the channel the whole game rests on is a different one."""
    e = Entities()
    aid = Contact(id=1, kind="aid", x=0, y=0, z=0, phase=0, drift_seed=1)
    hostile = Contact(id=2, kind="hostile", x=0, y=0, z=0, phase=0, drift_seed=1)
    for frame, want_aid, want_hostile in js["beacon"]:
        assert e.beacon_lit(aid, frame) is want_aid, f"aid at frame {frame}"
        assert e.beacon_lit(hostile, frame) is want_hostile


def test_hit_boxes_agree(js):
    e = Entities()
    for kind, want in js["hitBox"].items():
        box = e.hit_box(Contact(id=1, kind=kind, x=0, y=0, z=0, phase=0, drift_seed=1))
        assert box.half_w == pytest.approx(want["halfW"])
        assert box.half_h == pytest.approx(want["halfH"])


# ── Whole runs ──────────────────────────────────────────────────────


def _compare_run(name, want_frames, drive):
    e, p = Entities(), Player()
    for want in want_frames:
        f = want[0]
        drive(e, p, f)
        got_contacts = [[c.id, c.kind, c.x, c.y, c.z, c.doom_timer, c.locked_by]
                        for c in e.contacts]
        got_shots = [[s.x, s.y, s.z] for s in e.shots]
        got_events = [[ev.type, ev.contact.id] for ev in e.drain_events()]

        assert len(got_contacts) == len(want[1]), f"{name} frame {f}: contact count"
        for mine, theirs in zip(got_contacts, want[1], strict=True):
            assert mine[0] == theirs[0], f"{name} frame {f}: id"
            assert mine[1] == theirs[1], f"{name} frame {f}: kind"
            assert mine[2:] == pytest.approx(theirs[2:], rel=REL), f"{name} frame {f}"
        assert len(got_shots) == len(want[2]), f"{name} frame {f}: shot count"
        for mine, theirs in zip(got_shots, want[2], strict=True):
            assert mine == pytest.approx(theirs, rel=REL), f"{name} frame {f}: shot"
        assert got_events == [list(x) for x in want[3]], f"{name} frame {f}: events"
        assert len(e.particles) == want[4], f"{name} frame {f}: particles"


def test_a_whole_run_agrees(js):
    """Nine hundred frames flown by an aim-bot, which is what makes it useful:
    a wandering stick hits nothing, and friendly-fire attribution — the
    cruellest thing this game could get wrong — would go unexercised."""
    r = scripted(js["randoms"])

    def drive(e, p, f):
        t = config.difficulty_at(f * 5)
        axis_x = axis_y = 0.0
        nearest = None
        for c in e.contacts:
            if not c.dead and (nearest is None or c.z < nearest.z):
                nearest = c
        if nearest is not None:
            axis_x = max(-1.0, min(1.0, (nearest.x - p.x) / 30))
            axis_y = max(-1.0, min(1.0, (nearest.y - p.y) / 30))
        if p.update(axis_x, axis_y, True, 1):
            e.fire(p)
        e.update(p, config.rail_speed(t), t, 1, r)

    _compare_run("run", js["shots"], drive)


def test_a_passive_run_agrees(js):
    """Twelve hundred frames with the guns cold, which is the only way to see a
    convoy locked, its doom timer run down, and the convoy lost."""
    r = scripted(js["randoms"])

    def drive(e, p, f):
        t = config.difficulty_at(f * 5)
        p.update(0, 0, False, 1)
        e.update(p, config.rail_speed(t), t, 1, r)

    _compare_run("passive", js["passive"], drive)


def test_the_lock_and_the_rescue_agree(js):
    """A hostile placed beside a convoy, stepped twice: once left alone, once
    with the attacker killed partway through.

    The second is the rescue — the branch that clears the lock when the
    attacker dies, and the reason escorting is an active job rather than
    passive restraint. Constructed rather than waited for, because a rule this
    central should not be covered only when a run happens to produce it.
    """
    for kill_attacker, want_frames in js["lock"]:
        e, p = Entities(), Player()
        e.contacts.extend([
            Contact(id=1, kind="hostile", x=10, y=0, z=700, phase=0,
                    drift_seed=1, aggro=True),
            Contact(id=2, kind="aid", x=0, y=0, z=700, phase=0, drift_seed=1),
        ])
        e.next_id = 3
        e.spawn_timer = 1e9

        for want in want_frames:
            f = want[0]
            if kill_attacker and f == 60:
                for c in e.contacts:
                    if c.id == 1:
                        c.dead = True
            e.update(p, 0, 0, 1, scripted(js["randoms"]))

            got = [[c.id, c.kind, c.x, c.z, c.doom_timer, c.locked_by]
                   for c in e.contacts]
            assert len(got) == len(want[1]), f"kill={kill_attacker} f={f}: count"
            for mine, theirs in zip(got, want[1], strict=True):
                assert mine[0] == theirs[0] and mine[1] == theirs[1]
                assert mine[2:] == pytest.approx(theirs[2:], rel=REL)
            assert [[ev.type, ev.contact.id] for ev in e.drain_events()] == \
                [list(x) for x in want[2]], f"kill={kill_attacker} f={f}: events"


def test_the_oracle_actually_exercised_the_cruel_paths(js):
    """A comparison that covered only sightings and escorts would pass just as
    quietly as one that covered everything. This is what the first version of
    the oracle did, and the fix — a seeded generator and an aim-bot — is only
    worth anything if something checks it stayed fixed.
    """
    seen = {ev[0] for run in ("shots", "passive") for f in js[run] for ev in f[3]}
    seen |= {ev[0] for _, frames in js["lock"] for f in frames for ev in f[2]}
    for required in ("hostile-killed", "friendly-fire", "aid-locked",
                     "aid-lost", "aid-escorted", "aid-sighted"):
        assert required in seen, f"the oracle never produced a {required} event"


def test_the_run_covers_more_than_one_frame_of_flight(js):
    """Guards the shape of the fixtures themselves: an empty or truncated
    oracle would make every comparison above vacuous."""
    assert len(js["shots"]) == 900
    assert len(js["passive"]) == 1200
    assert len(js["randoms"]) >= 10000
    assert math.isclose(sum(js["randoms"]) / len(js["randoms"]), 0.5, abs_tol=0.02)
