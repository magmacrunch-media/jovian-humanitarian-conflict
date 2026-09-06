"""The rules, with no engine on the machine.

`jovian.config` and `jovian.projection` claim to import nothing outside the
standard library, so this suite runs with nothing but pytest installed. That is
not a convenience — it is the claim being tested, and
``test_the_rules_need_no_engine`` is what proves it rather than asserting it.

The web build's ``tests/test-simulation.js`` guards the same things from the
other side. Where a check exists in both, the numbers are the same numbers on
purpose: two implementations of one game that disagree about the fairness
budget is worse than either one being wrong alone.
"""

from itertools import pairwise

import pytest

from jovian import config, projection
from jovian.player import Player
from jovian.world import BAND_COUNT, World

# ── The fairness invariant ──────────────────────────────────────────
#
# The whole premise is that refusing to shoot is a decision rather than a
# gamble, which is only true if a convoy squawks long enough to be identified
# before it can possibly be shot. That is a relationship between four constants
# which a plausible-looking tuning pass can silently break: raise
# RAIL_SPEED_MAX for a bit more pace and the game is still fun and now unfair.


def test_the_fairness_invariant_holds_at_every_difficulty():
    """A convoy is identifiable before it can be shot, all the way up the ramp.

    Checked across the curve rather than only at the ends, because the budget
    is a ratio of two interpolations and nothing guarantees in advance that its
    worst point is an endpoint.
    """
    budget = config.TELEGRAPH_MIN_FRAMES + config.REACTION_FRAMES
    for i in range(101):
        t = i / 100
        assert config.identify_frames(t) >= budget, (
            f"at difficulty {t:.2f} a contact is identifiable for only "
            f"{config.identify_frames(t):.1f} frames, under the {budget} "
            f"frames the transponder and a human reaction need"
        )


def test_the_budget_is_tightest_at_full_difficulty():
    """Where the invariant binds, and by how much.

    The margin is the headroom a future tuning pass gets to spend. Pinning the
    figure means a change that eats it shows up as a diff on this line rather
    than as a game that quietly stops being fair.
    """
    budget = config.TELEGRAPH_MIN_FRAMES + config.REACTION_FRAMES
    tightest = config.identify_frames(1.0)
    assert tightest == pytest.approx(83.33, abs=0.01)
    assert budget == 78
    assert tightest / budget == pytest.approx(1.068, abs=0.001)


def test_two_full_squawk_cycles_fit_in_the_telegraph_window():
    """TELEGRAPH_MIN_FRAMES is not a round number somebody liked.

    It is two complete blink cycles, which is what it takes to read a
    double-tap as a double-tap rather than as two unrelated flashes.
    """
    assert config.TELEGRAPH_MIN_FRAMES == 2 * config.BLINK_PERIOD


def test_the_transponder_is_a_double_tap_not_a_pulse():
    """The windows have to leave a gap, or the two taps merge into one blink."""
    assert config.BLINK_ON_1 < config.BLINK_GAP < config.BLINK_ON_2
    assert config.BLINK_ON_2 < config.BLINK_PERIOD


# ── The projection ──────────────────────────────────────────────────


def test_scale_is_exactly_one_at_the_players_plane():
    """Not approximately: the ship is drawn without going through this at all,
    so anything else would put it at a different size from a contact beside
    it."""
    assert projection.scale_at(0) == 1.0


def test_scale_falls_off_monotonically():
    """What lets draw order be a plain sort on z.

    A transform that was not monotonic would let a nearer contact draw behind a
    further one, which in a game about identifying what you are looking at
    would hide the answer.
    """
    previous = float("inf")
    for z in range(config.Z_NEAR, config.Z_FAR + 1, 5):
        s = projection.scale_at(z)
        assert s < previous, f"scale did not decrease at z={z}"
        previous = s


def test_the_documented_scale_table_is_the_one_it_computes():
    """config.py's comment quotes four measured scales. This is that comment,
    checked — a docstring that drifts from the code is worse than none."""
    assert projection.scale_at(0) == pytest.approx(1.00, abs=0.005)
    assert projection.scale_at(410) == pytest.approx(0.56, abs=0.005)
    assert projection.scale_at(config.Z_FIRE_MAX) == pytest.approx(0.46, abs=0.005)
    assert projection.scale_at(config.Z_FAR) == pytest.approx(0.32, abs=0.005)


def test_depth_at_inverts_scale_at():
    for z in (0, 100, 410, 600, 1100, 3200):
        assert projection.depth_at(projection.scale_at(z)) == pytest.approx(z, abs=1e-6)


def test_the_vanishing_point_is_where_every_depth_lands():
    """The definition, asserted as one.

    A point at the camera's own x/y projects to the vanishing point no matter
    how far away it is. If this ever stopped holding, the horizon would slide
    against the rail and the depth cue would come apart.
    """
    for cam_x, cam_y in ((0, 0), (60, -30), (-140, 84)):
        want = projection.vanishing(cam_x, cam_y)
        for z in (config.Z_NEAR, 0, 410, config.Z_FAR, 5000):
            got = projection.point(cam_x, cam_y, z, cam_x, cam_y)
            assert (got.x, got.y) == pytest.approx(want, abs=1e-9)


def test_the_scale_comes_back_with_the_point():
    """Callers need it to size what they are drawing, and a second call is a
    chance for the two to disagree."""
    p = projection.point(10, 20, 410, 0, 0)
    assert p.s == projection.scale_at(410)


def test_banking_swings_the_horizon_the_other_way():
    """The PARALLAX term, which is what sells the depth.

    The world shifts one way with the camera and the vanishing point shifts the
    other. Without the second term the rail reads as a flat scrolling backdrop
    however correct the divide is.
    """
    centred = projection.vanishing(0, 0)[0]
    leaning_left = projection.vanishing(-100, 0)[0]
    assert leaning_left < centred


# ── Collision ───────────────────────────────────────────────────────


def test_in_box_is_a_box_and_not_a_radius():
    """The corner case, literally.

    A point at the corner of the box is inside it. A circle sized to the
    half-width would have to miss it, and a circle sized to the diagonal would
    swallow space the hull does not occupy.
    """
    half_w, half_h = 18, 10
    assert projection.in_box(0, 0, half_w, half_h, half_w, half_h)
    assert not projection.in_box(0, 0, half_w + 0.001, half_h, half_w, half_h)
    assert not projection.in_box(0, 0, half_w, half_h + 0.001, half_w, half_h)


def test_the_hit_margins_favour_the_player_in_both_directions():
    """A hostile is easier to hit than it looks; a convoy is exactly as big as
    it looks. The first cut had this backwards and every near miss punished you
    twice — the hostile survived and the convoy behind it took the shot."""
    assert config.HOSTILE_HIT_MARGIN_W > 0
    assert config.HOSTILE_HIT_MARGIN_H > 0
    assert config.AID_HIT_MARGIN_W == 0
    assert config.AID_HIT_MARGIN_H == 0


def test_firing_range_sits_well_inside_the_readable_window():
    """You can never shoot something you were not given the chance to read.

    Silhouettes resolve at Z_SHAPE_READABLE and shots stop at Z_FIRE_MAX, so
    the ordering here is what makes "no shot you lacked the information to
    hold" true of the third channel as well as the first.
    """
    assert config.Z_FIRE_MAX > config.Z_SHAPE_READABLE
    assert config.Z_PING > config.Z_FAR - (config.Z_FAR - config.Z_FIRE_MAX)


# ── The difficulty curve ────────────────────────────────────────────


def test_difficulty_clamps_at_both_ends():
    assert config.difficulty_at(-500) == 0.0
    assert config.difficulty_at(0) == 0.0
    assert config.difficulty_at(config.DIFFICULTY_DISTANCE) == 1.0
    assert config.difficulty_at(config.DIFFICULTY_DISTANCE * 10) == 1.0


def test_the_curve_only_ever_makes_the_game_harder():
    """Every interpolated quantity moves one way across the ramp. A pair that
    crossed would make some middle difficulty easier than the start, which is
    not a curve anybody intended."""
    early, late = 0.0, 1.0
    assert config.rail_speed(late) > config.rail_speed(early)
    assert config.spawn_interval(late) < config.spawn_interval(early)
    assert config.aggro(late) > config.aggro(early)
    # Flat on purpose: the moral pressure must not thin out as it gets busier.
    assert config.aid_share(late) == pytest.approx(0.26)
    assert config.aid_share(early) == pytest.approx(0.30)


def test_wave_size_is_a_floor_plus_a_coin_flip():
    """The fractional part is a per-wave chance, not a rounding."""
    assert config.wave_size(0.0, lambda: 0.0) == 2
    assert config.wave_size(0.0, lambda: 0.999) == 2
    assert config.wave_size(1.0, lambda: 0.0) == 3
    assert config.wave_size(0.5, lambda: 0.0) == 3   # raw 2.5, flip wins
    assert config.wave_size(0.5, lambda: 0.9) == 2   # raw 2.5, flip loses


def test_the_opening_wave_shows_both_kinds_side_by_side():
    """A wave of one is either a hostile or a convoy, never both, and the
    difference between them is the only thing this game asks you to learn."""
    assert config.WAVE_SIZE_EARLY >= 2


def test_a_convoy_can_be_saved_after_it_is_locked():
    """AID_KILL_FRAMES has a floor of 114 frames — the time a perfect player
    needs to cross the rail, land a shot, react and clear the gun cooldown. The
    first tuning was 105, which was not a tight window but an impossible one."""
    assert config.AID_KILL_FRAMES >= 114


# ── The rail ────────────────────────────────────────────────────────


def _rail():
    """A world built from a fixed sequence, so a failure is reproducible."""
    seq = iter([(i * 0.6180339887) % 1.0 for i in range(1, 5000)])
    w = World()
    w.reset(lambda: next(seq))
    return w


def test_the_deck_bands_are_evenly_spaced():
    w = _rail()
    assert len(w.bands) == BAND_COUNT
    gaps = [b - a for a, b in pairwise(w.bands)]
    assert gaps == pytest.approx([config.DECK_Z_SPAN / BAND_COUNT] * len(gaps))


def test_the_bands_keep_their_spacing_across_a_large_step():
    """The wrap adds the span rather than assigning it, so an enormous dt
    cannot collapse the ring into a clump at zero — which is what an assignment
    would do, and which reads on screen as the deck stopping dead."""
    w = _rail()
    p = Player()
    for _ in range(40):
        w.update(p, config.RAIL_SPEED_MAX, 30)

    ordered = sorted(w.bands)
    gaps = [b - a for a, b in pairwise(ordered)]
    assert gaps == pytest.approx([config.DECK_Z_SPAN / BAND_COUNT] * len(gaps))
    assert all(0 < b <= config.DECK_Z_SPAN for b in w.bands)


def test_the_camera_closes_on_the_ship_but_never_overshoots():
    """A proportional chase. Overshoot would be visible as the horizon
    wobbling past the ship and settling back."""
    w = _rail()
    p = Player()
    p.x, p.y = 120.0, 60.0

    previous = -1.0
    for _ in range(200):
        w.update(p, 0, 1)
        assert w.cam_x > previous, "the camera should be closing, monotonically"
        assert w.cam_x <= p.x, "and never past the ship"
        previous = w.cam_x
    assert w.cam_x == pytest.approx(p.x, abs=0.5)


def test_the_camera_follows_only_half_the_vertical():
    """Climbing lifts the horizon less than it lifts the ship, which is what
    keeps the ship readable against the deck instead of pinned to it."""
    w = _rail()
    p = Player()
    p.x, p.y = 0.0, 80.0
    for _ in range(400):
        w.update(p, 0, 1)
    assert w.cam_y == pytest.approx(p.y * 0.5, abs=0.5)


def test_stars_are_fixed_to_the_backdrop():
    """They read as infinitely far away, so nothing about the rail moves them."""
    w = _rail()
    before = [(s.x, s.y, s.brightness) for s in w.stars]
    p = Player()
    for _ in range(300):
        w.update(p, config.RAIL_SPEED_MAX, 1)
    assert [(s.x, s.y, s.brightness) for s in w.stars] == before


def test_the_rail_needs_no_randomness_of_its_own():
    """reset takes its randomness as an argument, so a seeded run is
    reproducible and a test never depends on global state."""
    a, b = World(), World()
    seq = [(i * 0.31) % 1.0 for i in range(500)]
    a.reset(iter(seq).__next__)
    b.reset(iter(seq).__next__)
    assert [(s.x, s.y) for s in a.stars] == [(s.x, s.y) for s in b.stars]


# ── The seam ────────────────────────────────────────────────────────


def test_the_rules_need_no_engine():
    """config and projection import nothing but the standard library.

    This is the claim that lets the suite above run on a bare checkout, and the
    property that keeps the rules portable to whatever draws them next.
    """
    import subprocess
    import sys

    code = (
        "import sys, jovian.config, jovian.projection, "
        "jovian.player, jovian.entities, jovian.world; "
        "print([m for m in sys.modules "
        "if m.startswith('magmacrunch') or m == 'textual' or m == 'rich'])"
    )
    out = subprocess.run([sys.executable, "-c", code], capture_output=True,
                         text=True, check=True)
    assert out.stdout.strip() == "[]", out.stdout
