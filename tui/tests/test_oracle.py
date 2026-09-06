"""The Python port, checked against the JavaScript that actually ships.

`test_physics.py` proves the port is internally consistent and obeys the rules
the game claims. It cannot prove the transcription is *faithful* — a constant
mistyped in `jovian/config.py` gives a self-consistent game that is not this
game, and the first symptom is a difficulty curve that feels subtly wrong on a
build nobody thinks to compare.

So this runs the real `web/js/config.js` and `web/js/projection.js` in a node
vm and compares every number. Same method as texas-holdem-lava-dome, which
checks its hand evaluator against the shipped AdCards the same way.

Skipped where node is unavailable rather than failed: the port has to remain
testable on a machine with only Python, which `test_physics.py` guarantees on
its own. CI has node — the web job already needs it — so this gates every push
even though it is skippable here.
"""

import json
import pathlib
import shutil
import subprocess

import pytest

from jovian import config, projection

ORACLE = pathlib.Path(__file__).resolve().parent.parent / "tools" / "js_oracle.mjs"


@pytest.fixture(scope="module")
def js():
    if shutil.which("node") is None:
        pytest.skip("needs node to run the shipped JS")
    out = subprocess.run(["node", str(ORACLE)], capture_output=True, text=True)
    if out.returncode != 0:
        pytest.fail(f"oracle failed:\n{out.stderr}")
    return json.loads(out.stdout)


def test_every_constant_matches_the_shipped_config(js):
    """Field by field, so a typo shows up as the number it is.

    Completeness is asserted rather than a floor: every numeric constant the
    shipped config declares is ported, all 67 of them, and a new one added to
    the web build should fail here until somebody decides whether the terminal
    wants it. A floor would let that decision be skipped silently, which is how
    the two builds start drifting.

    Only *numeric* constants are in scope. The palette is hex strings and the
    music block is a URL and a pair of fade times; the oracle filters those out
    on its side, because a terminal has neither.
    """
    missing = [name for name in js["config"] if getattr(config, name, None) is None]
    assert not missing, (
        "the web build declares constants this port does not: "
        + ", ".join(sorted(missing))
        + " — port them, or record here why the terminal does not want them"
    )

    mismatched = {}
    for name, value in js["config"].items():
        ours = getattr(config, name)
        if ours != pytest.approx(value):
            mismatched[name] = (ours, value)

    assert not mismatched, "python != javascript: " + ", ".join(
        f"{k} is {a} here and {b} there" for k, (a, b) in mismatched.items()
    )
    # A comparison that silently checked nothing would pass just as quietly.
    assert len(js["config"]) >= 60, f"oracle only offered {len(js['config'])}"


def test_the_perspective_divide_agrees(js):
    for z, want in js["scaleAt"]:
        assert projection.scale_at(z) == pytest.approx(want, rel=1e-12)


def test_the_inverse_agrees(js):
    for s, want in js["depthAt"]:
        assert projection.depth_at(s) == pytest.approx(want, rel=1e-12)


def test_every_projected_point_agrees(js):
    """The whole grid: three world points at twelve depths under four cameras.

    Both terms of the transform matter here — the parallax slide is only
    exercised when the camera is off centre, and a port that dropped it would
    still pass every camera-at-origin case.
    """
    for wx, wy, z, cx, cy, want in js["point"]:
        got = projection.point(wx, wy, z, cx, cy)
        assert got.x == pytest.approx(want["x"], rel=1e-12), f"x at z={z} cam={cx},{cy}"
        assert got.y == pytest.approx(want["y"], rel=1e-12), f"y at z={z} cam={cx},{cy}"
        assert got.s == pytest.approx(want["s"], rel=1e-12), f"s at z={z}"


def test_the_vanishing_point_agrees(js):
    for cx, cy, want in js["vanishing"]:
        got = projection.vanishing(cx, cy)
        assert got == pytest.approx((want["x"], want["y"]), rel=1e-12)


#: The one place the two naming conventions meet: a JS camelCase key against
#: the Python function that ports it.
CURVE = {
    "railSpeed": config.rail_speed,
    "spawnInterval": config.spawn_interval,
    "aidShare": config.aid_share,
    "aggro": config.aggro,
    "identifyFrames": config.identify_frames,
}


def test_the_difficulty_curve_agrees(js):
    """Every interpolation, at five points along the ramp."""
    for t, want in js["difficulty"]:
        for key, fn in CURVE.items():
            assert fn(t) == pytest.approx(want[key], rel=1e-12), f"{key} at t={t}"


def test_the_difficulty_ramp_agrees(js):
    for distance, want in js["difficultyAt"]:
        assert config.difficulty_at(distance) == pytest.approx(want, rel=1e-12)
