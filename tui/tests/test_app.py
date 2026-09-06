"""The screens, driven headlessly.

These cover what the simulation suites cannot: that the rules are wired to the
screen correctly, and that the title and a run hand off to each other. They
need the engine and its terminal extra, and are skipped without them so the
simulation tests still run on a bare checkout.

Textual's ``run_test`` pilot gives a real app with a real event loop and a real
size, so key handling, the frame loop and resize are exercised as they are in
play — no mocking of the parts most likely to break.
"""

import asyncio

import pytest

pytest.importorskip("textual", reason='needs: pip install -e ".[dev]"')

from magmacrunch.engine import scores as score_mod  # noqa: E402
from magmacrunch.engine.arcade import ArcadeGame  # noqa: E402
from magmacrunch.engine.core.tui_host import TuiHost  # noqa: E402

from jovian import config, theme  # noqa: E402
from jovian.app import JovianApp  # noqa: E402
from jovian.arcade import GAME  # noqa: E402
from jovian.entities import Contact, Event  # noqa: E402
from jovian.scenes import TitleScene  # noqa: E402


@pytest.fixture(autouse=True)
def isolated_scores(tmp_path, monkeypatch):
    """No test may touch a real player's score file."""
    monkeypatch.setenv(score_mod.DATA_DIR_ENV, str(tmp_path))


def buffer_text(app: JovianApp) -> str:
    return app.host.game.surface.buffer.to_text()


def settle(app: JovianApp) -> None:
    app.host.stack.update(0.0)


def hosted(seed: int = 7) -> JovianApp:
    host = TuiHost(title=GAME.info.title, fps=GAME.info.fps,
                   hold_ms=GAME.info.hold_ms)
    app = JovianApp(host, seed=seed)
    host.push_scene(app.root_scene)
    settle(app)
    return app


def flying(seed: int = 7):
    app = hosted(seed)
    app.start_run()
    settle(app)
    return app, app.host.scene


async def _piloted(app: JovianApp, size=(80, 24)):
    from magmacrunch.engine.core.tui_game import _GameApp

    textual_app = _GameApp(app.host.game, app.host.game.surface)
    app.host.game._app = textual_app
    return textual_app.run_test(size=size)


def run(coro):
    return asyncio.run(coro)


def tap(app: JovianApp, key: str) -> None:
    """A keypress the way the host delivers one."""
    app.host.input.press(key)
    for pressed in app.host.input.drain():
        app.host.stack.dispatch_key(pressed)


# ── The declaration ─────────────────────────────────────────────────


def test_the_game_is_a_valid_arcade_cabinet():
    assert isinstance(GAME, ArcadeGame)
    assert GAME.info.key == "jovian-humanitarian-conflict"


def test_it_is_the_first_cabinet_that_wants_held_state():
    """hold_ms above zero, which no other cabinet here asks for.

    Steering is a held direction — overshoot survivable, the stakes only which
    way the ship drifts — which is precisely what TuiInput's docstring says
    decay is for. Firing is the opposite and is read as an edge instead, so
    the two live on one input source without fighting.
    """
    assert GAME.info.fps == 30
    assert GAME.info.hold_ms == 180


def test_start_returns_a_scene_without_pushing_it():
    host = TuiHost(title="t", fps=GAME.info.fps, hold_ms=GAME.info.hold_ms)
    scene = GAME.start(host)
    assert isinstance(scene, TitleScene)
    assert len(host.stack) == 0


def test_listing_this_game_does_not_drag_in_its_screens():
    """A menu loads one arcade module per installed game just to draw a row."""
    import subprocess
    import sys

    code = ("import sys, jovian.arcade; "
            "print([m for m in ('jovian.scenes', 'jovian.app', 'jovian.entities', "
            "'textual') if m in sys.modules])")
    out = subprocess.run([sys.executable, "-c", code], capture_output=True,
                         text=True, check=True)
    assert out.stdout.strip() == "[]", out.stdout


# ── The stack ───────────────────────────────────────────────────────


def test_the_title_is_the_bottom_of_the_stack():
    app = hosted()
    assert isinstance(app.host.scene, TitleScene)
    assert not app.in_game


def test_starting_a_run_pushes_it_over_the_title():
    app, scene = flying()
    assert app.in_game
    assert len(app.host.stack) == 2
    assert isinstance(app.host.stack.scenes[0], TitleScene)


def test_escape_pops_back_to_the_title():
    app, scene = flying()
    scene.handle_key("escape")
    settle(app)
    assert isinstance(app.host.scene, TitleScene)


# ── The two verbs ───────────────────────────────────────────────────


def test_steering_reads_held_state():
    """The decay half of the seam: holding right moves the ship right."""
    app, scene = flying()
    start = scene.player.x
    app.host.input.press("right")
    for _ in range(20):
        app.host.stack.update(1 / 30)
    assert scene.player.x > start


def test_firing_is_an_edge_and_never_held_state():
    """The other half. Holding the fire key down must not keep firing, because
    the moment of pressing is the game — a held trigger would make shooting a
    convoy something the player did not decide to do."""
    app, scene = flying()
    app.host.input.press("z")
    for _ in range(60):
        app.host.stack.update(1 / 30)
    assert scene.entities.shots == [], "held fire should produce no shots at all"


def test_a_press_fires_exactly_once():
    app, scene = flying()
    tap(app, "z")
    assert len(scene.entities.shots) == 1
    # Again immediately: the cooldown holds it.
    tap(app, "z")
    assert len(scene.entities.shots) == 1


def test_the_gun_cools_down_and_fires_again():
    """Stepped in real frames, not in seconds.

    This used a step of 1 when the scene took dt in frames. It now takes
    seconds, so a step of 1 is sixty frames — eleven seconds of game time
    before the second shot, by which point the run is over and firing is
    correctly refused.
    """
    app, scene = flying()
    tap(app, "z")
    for _ in range(12):          # 12/30 s = 24 frames, clear of the 9-frame cooldown
        app.host.stack.update(1 / 30)
    assert not scene.dead, "the run should still be going"
    tap(app, "z")
    assert len(scene.entities.shots) == 2


# ── Pause ───────────────────────────────────────────────────────────


def test_p_stops_the_clock():
    app, scene = flying()
    for _ in range(5):
        app.host.stack.update(1 / 30)
    scene.handle_key("p")
    frozen = scene.frame
    for _ in range(10):
        app.host.stack.update(1 / 30)
    assert scene.paused
    assert scene.frame == frozen


def test_a_pause_cannot_be_used_to_keep_firing():
    app, scene = flying()
    scene.handle_key("p")
    tap(app, "z")
    assert scene.entities.shots == []


def test_a_finished_run_cannot_be_paused():
    app, scene = flying()
    scene.dead = True
    scene.handle_key("p")
    assert not scene.paused


# ── Scoring ─────────────────────────────────────────────────────────


def _contact(kind, cid=1):
    return Contact(id=cid, kind=kind, x=0, y=0, z=100, phase=0, drift_seed=1)


def test_killing_a_hostile_scores_and_builds_the_streak():
    app, scene = flying()
    scene.entities.events.append(Event("hostile-killed", _contact("hostile")))
    scene._score_events()
    assert scene.score == config.SCORE_HOSTILE
    assert scene.combo == 2


def test_escorting_a_convoy_scores_more_than_a_kill():
    app, scene = flying()
    scene.entities.events.append(Event("aid-escorted", _contact("aid")))
    scene._score_events()
    assert scene.score == config.SCORE_ESCORT
    assert config.SCORE_ESCORT > config.SCORE_HOSTILE


def test_friendly_fire_costs_points_a_strike_and_the_streak():
    """The attribution the whole game turns on."""
    app, scene = flying()
    scene.combo = 4
    scene.entities.events.append(Event("friendly-fire", _contact("aid")))
    scene._score_events()
    assert scene.score == config.SCORE_FRIENDLY_FIRE
    assert scene.strikes == 1
    assert scene.combo == 1


def test_a_convoy_lost_to_hostiles_costs_the_streak_but_not_a_strike():
    """The other half of that attribution. Losing a convoy you were escorting
    is a failure; it is not the failure that ends the run."""
    app, scene = flying()
    scene.combo = 4
    scene.entities.events.append(Event("aid-lost", _contact("aid")))
    scene._score_events()
    assert scene.strikes == 0
    assert scene.combo == 1
    assert scene.score == 0


def test_three_strikes_ends_the_run_whatever_the_score():
    app, scene = flying()
    scene.score = 99999
    for i in range(config.MAX_STRIKES):
        scene.entities.events.append(Event("friendly-fire", _contact("aid", i + 1)))
        scene._score_events()
    app.host.stack.update(1 / 30)
    assert scene.dead


def test_running_out_of_lives_ends_the_run():
    app, scene = flying()
    scene.player.lives = 0
    app.host.stack.update(1 / 30)
    assert scene.dead


# ── Drawing ─────────────────────────────────────────────────────────


def test_the_title_screen_says_the_name_and_the_iff_key():
    app = hosted()

    async def go():
        async with await _piloted(app) as pilot:
            await pilot.pause()
            await asyncio.sleep(0.3)
            text = buffer_text(app)
            assert "FLY THE RAIL" in text
            assert "escort" in text and "destroy" in text
            app.host.quit()

    run(go())


def test_the_iff_key_never_overlaps_itself():
    """It did, at 80 columns, when the two halves were placed at hardcoded
    offsets. It is the one line on that screen that must not be ambiguous."""
    app = hosted()

    async def go():
        for size in ((80, 24), (60, 20), (100, 30), (140, 40)):
            async with await _piloted(app, size=size) as pilot:
                await pilot.pause()
                await asyncio.sleep(0.3)
                text = buffer_text(app)
                assert "squawks" in text, f"{size}: no IFF key"
                assert "escort" in text and "destroy" in text, f"{size}: truncated"
            app.host.quit()

    run(go())


def test_a_run_draws_the_ship_the_hud_and_the_contact_strip():
    app, scene = flying()

    async def go():
        async with await _piloted(app) as pilot:
            await pilot.pause()
            for _ in range(120):
                app.host.stack.update(1 / 30)
            await asyncio.sleep(0.3)
            text = buffer_text(app)
            assert "SCORE" in text
            assert theme.SHIP in text, "the ship should be on screen"
            assert theme.LIFE_GLYPH in text, "lives should be in the header"
            app.host.quit()

    run(go())


def test_a_terminal_below_the_floor_is_told_so_rather_than_drawn_in():
    app, scene = flying()

    async def go():
        async with await _piloted(app, size=(30, 10)) as pilot:
            await pilot.pause()
            await asyncio.sleep(0.3)
            text = buffer_text(app)
            assert "TOO SMALL" in text
            assert "SCORE" not in text
            app.host.quit()

    run(go())


def test_resizing_refits_the_frame_rather_than_keeping_a_stale_one():
    app, scene = flying()

    async def go():
        async with await _piloted(app, size=(60, 20)) as pilot:
            await pilot.pause()
            await asyncio.sleep(0.3)
            narrow = buffer_text(app)
            await pilot.resize_terminal(120, 40)
            await asyncio.sleep(0.35)
            wide = buffer_text(app)
            assert len(wide.split("\n")) > len(narrow.split("\n"))
            app.host.quit()

    run(go())


# ── The rail, and the unit that drives it ───────────────────────────


def test_a_second_of_wall_clock_is_sixty_frames_of_game():
    """The engine measures dt in seconds; every ported constant is per 60fps
    frame. Handed straight through, the game ran about sixty times too slow —
    the ship crawled, the rail barely moved, and nothing looked broken enough
    to be obviously wrong. Moonlight Drift never met this because it is
    frame-locked and takes no dt at all.
    """
    app, scene = flying()
    for _ in range(30):
        app.host.stack.update(1 / 30)
    expected = config.rail_speed(0) * 60
    assert scene.world.distance == pytest.approx(expected, rel=0.02)


def test_the_ship_crosses_the_rail_in_about_a_second():
    """The feel that conversion buys, stated as a number. At SHIP_SPEED_MAX the
    ship covers 324 world units a second against a 168-unit half-rail."""
    app, scene = flying()
    app.host.input.press("right")
    for _ in range(30):
        app.host.stack.update(1 / 30)
    assert scene.player.x == pytest.approx(config.SHIP_X_RANGE, abs=1)


def test_the_deck_leaves_gaps_between_its_bands():
    """A canvas draws a band as a one-to-three pixel line in a hundred and
    fifty pixel deck, so thirty of them still leave mostly gap — and the gap is
    what the motion reads against. A cell has no fraction of a row, so the
    first version filled every row below the horizon with tildes and the depth
    cue vanished into a wall of texture.
    """
    app, scene = flying()

    async def go():
        async with await _piloted(app) as pilot:
            await pilot.pause()
            for _ in range(60):
                app.host.stack.update(1 / 30)
            await asyncio.sleep(0.3)
            lines = buffer_text(app).splitlines()
            below = [line for line in lines[12:22]]
            banded = [line for line in below if line.count("~") > 10]
            blank = [line for line in below if line.count("~") == 0]
            assert banded, "no deck bands drawn at all"
            assert blank, "every row below the horizon is a band; no gap left"
            app.host.quit()

    run(go())


def test_the_rails_report_which_way_the_camera_leans():
    """Two lines converging on the vanishing point, and the reason world.js
    gives for them: without them, sliding left and sliding the whole world
    right look identical, so the drift that sells the depth is invisible.

    Rendered synchronously, one frame, with no live loop.

    Getting here took two goes. Holding a key decays on a *real* clock, so the
    first version passed alone and failed under load. Setting the camera and
    then running a pilot is no better: the pilot advances the game, and the
    camera chases back toward the ship while the test is watching — which
    failed about one run in six. A drawing test should depend on nothing but
    what it drew.
    """
    def rails_at(cam_x):
        app, scene = flying()
        scene.world.cam_x = cam_x
        scene.render()
        lines = buffer_text(app).splitlines()
        marks = [line for line in lines if line.count(".") >= 2 and "~" not in line]
        assert marks, "the rails should always be drawn"
        return [i for i, c in enumerate(marks[-1]) if c == "."]

    left, centre, right = rails_at(-150.0), rails_at(0.0), rails_at(150.0)
    # Leaning left swings the world right, and the rails with it.
    assert left[0] > centre[0], "leaning left should push the rails right"
    assert right[0] < centre[0], "leaning right should push them left"


def test_the_game_help_fits_the_smallest_terminal():
    """`_fit` truncates from the right and says nothing about it, so a hint
    line that outgrows the floor loses its last key silently."""
    from jovian.scenes import GAME_HELP

    assert len(GAME_HELP) <= theme.MIN_COLS - 2
