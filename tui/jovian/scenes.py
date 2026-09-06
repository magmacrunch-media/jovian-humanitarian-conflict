"""The screens, as scenes.

Modality is the stack, not a flag — the engine's own rule. :class:`TitleScene`
sits at the bottom; starting a run pushes a :class:`GameScene` on top of it,
and Esc pops back.

**This is the first cabinet whose two verbs land on opposite sides of the
engine's own input seam**, and it is worth saying why here rather than leaving
it to be rediscovered. ``TuiInput``'s docstring draws the line: decay semantics
suit a *held direction*, where a little overshoot is survivable and the stakes
are which way something drifts; they do not suit a control where the exact
moment of pressing is the game.

Steering is the first of those. It feeds an accelerate-and-coast model with
drag, overshoot costs nothing, and the touch backend on the web already proves
the axis tolerates going quiet — a lifted thumb coasts rather than snapping to
centre. So it reads held state through ``is_pressed`` and the cabinet asks for
``hold_ms`` above zero, which no cabinet here has done before.

Firing is the second. The whole game is that refusing to shoot is a decision,
so the moment of pressing *is* the game; it arrives as a discrete press through
:meth:`GameScene.handle_key` and never from held state. Auto-fire would delete
the game rather than simplify it.
"""

from __future__ import annotations

import textwrap
from dataclasses import replace

from magmacrunch.engine import scores
from magmacrunch.engine.ui import bigtext
from magmacrunch.engine.ui.menu import Menu
from magmacrunch.engine.ui.theme import DEFAULT_THEME

from jovian import cells, config, projection, theme
from jovian.entities import Entities
from jovian.player import Player
from jovian.world import World

MENU_HELP = "↑↓ choose    Enter select    Q quit"
ARCADE_HELP = "Esc  back to the arcade"
#: Two spaces between hints where the other lines use four. Six hints do not
#: fit the 60-column floor otherwise, and `_fit` truncates from the right
#: without saying so — the hint that goes missing is always the last one,
#: which here is how to get out.
GAME_HELP = "←→↑↓ fly  Z fire  P pause  R restart  Esc title  Q quit"
RULES_HELP = "↑↓ scroll    any other key goes back"
INITIALS_HELP = "Enter confirms    Backspace fixes"
SCORES_HELP = "any key goes back"

#: What the ported constants are measured in.
#:
#: **The one unit conversion in the game, and it belongs at exactly one line.**
#: Every rate in :mod:`jovian.config` is "units per 60fps frame" and is
#: multiplied by dt at the point of use — the web build's convention, and what
#: makes the simulation frame-rate independent and comparable against the
#: shipped JavaScript. The engine's loop measures dt in *seconds*
#: (``now - self._last_time``), so handing it straight to the simulation runs
#: the game about sixty times too slow: the ship crawls, the rail barely moves,
#: and nothing looks broken enough to be obviously wrong.
#:
#: Moonlight Drift never met this because it is frame-locked — its update takes
#: no dt at all and steps once per call. This game inherits real dt scaling
#: from the browser, so it has to say what a dt of one means.
FRAMES_PER_SECOND = 60.0

#: The deck's surface, in world units, and how wide a band is drawn.
#:
#: Locals in ``world.js``'s drawDeck, promoted here because a terminal draws
#: the same two numbers in two places — the bands and the rails — and they have
#: to agree or the rails leave the deck. DECK_Y sits below the ship's box
#: rather than on it, so the deck reads as ground the rail runs over.
DECK_Y = 150
DECK_HALF_W = 900

#: Rows a drawn band must clear before another is drawn. Two leaves a gap
#: between every pair, which is what the motion reads against.
DECK_ROW_GAP = 2

#: Where the converging rails meet the bottom of the frame.
RAIL_NEAR_X = 210
RAIL_NEAR_Z = 20

#: Keys that fire. Read as *key names* in handle_key rather than as the
#: engine's button "a", because firing must be an edge and the input source is
#: running with decay for the sake of steering.
FIRE_KEYS = ("z", "space")

RULES = (
    ("THE RULE", (
        "Aid convoys fly among the hostiles. Shoot one and it costs you a "
        "thousand points and one of three strikes; three ends the run "
        "whatever the score says.",
        "Everything you need to tell them apart arrives before anything can "
        "be shot. That is arithmetic, not courtesy — a convoy completes two "
        "full transponder cycles before it reaches firing range.",
    )),
    ("THE TRANSPONDER", (
        "A convoy squawks a double blink: on, off, on, then dark. Hostiles "
        "never light at all, and the absence is the signal.",
        "The blink is drawn at the same size however far away a contact is, "
        "so it is readable from the frame it appears on — unlike its "
        "silhouette, which only resolves once it is close.",
    )),
    ("READING THE RAIL", (
        "Depth is width. Contacts converge toward the vanishing point as they "
        "recede, so a mark near the centre of the frame is far away and one "
        "out at the edges is nearly on you.",
        "The strip along the bottom lists every contact and its depth, so a "
        "crowded frame never hides the answer.",
    )),
    ("ESCORTING", (
        "Hostiles break off to attack convoys. A convoy under fire is marked "
        "and has a little under three seconds before it is lost — kill its "
        "attacker and the mark clears.",
        "A convoy that reaches you alive is worth five hundred. A hostile is "
        "a hundred, multiplied by your streak.",
    )),
)


def _fit(text: str, width: int) -> str:
    if width <= 1 or len(text) <= width:
        return text
    return text[:width - 1] + "…"


def _too_small(renderer, cols: int, rows: int) -> bool:
    """Say so rather than drawing a clipped screen."""
    if renderer.width >= cols and renderer.height >= rows:
        return False
    renderer.ui_text(1, 1, "TERMINAL TOO SMALL", fill=theme.WARN)
    renderer.ui_text(1, 2, f"need {cols}x{rows}, "
                           f"have {renderer.width}x{renderer.height}",
                     fill=theme.HUD_DIM)
    return True


def _menu_theme():
    return replace(
        DEFAULT_THEME,
        primary=theme.MENU_SELECTED,
        text=theme.HUD_TEXT,
        dim_text=theme.HUD_DIM,
        box_fill=theme.MENU_BOX,
        box_outline=theme.DECK_LINE,
        outline_width=1,
        selection_fill=theme.MENU_SELECTION_BG,
    )


def _draw_title(renderer, cx: int, box_top: int) -> int:
    """The name, set as large as the window allows. Returns the row below it."""
    budget = box_top - 1
    for big, rest in theme.TITLE_LADDER:
        needed = bigtext.height(big) + (1 if rest else 0)
        if bigtext.width(big) > renderer.width - 2 or needed > budget:
            continue
        y = 1
        for line in bigtext.lines(big):
            renderer.ui_text(cx, y, line, fill=theme.TITLE, anchor="n")
            y += 1
        if rest:
            renderer.ui_text(cx, y, rest, fill=theme.SUBTITLE, anchor="n")
            y += 1
        return y
    renderer.ui_text(cx, 1, theme.BANNER, fill=theme.TITLE, anchor="n")
    return 2


class TitleScene:
    """Name, the IFF key, and a way in."""

    ITEMS = ("FLY THE RAIL", "HOW TO PLAY", "HIGH SCORES", "QUIT")

    def __init__(self, app):
        self.app = app
        self.menu = Menu(
            app.renderer,
            theme=_menu_theme(),
            menu_width=theme.MENU_W,
            item_height=theme.MENU_ITEM_H,
            title_height=theme.MENU_TITLE_H,
            item_padding=theme.MENU_PAD,
            border_pad=theme.MENU_BORDER,
            selected_color=theme.MENU_SELECTED,
            normal_color=theme.HUD_TEXT,
        )
        self._show()

    def _show(self) -> None:
        self.menu.show(list(self.ITEMS), on_select=self._chose)

    def _chose(self, index: int, label: str) -> None:  # noqa: ARG002
        if index == 0:
            self.app.start_run()
        elif index == 1:
            self.app.show_rules()
        elif index == 2:
            self.app.show_scores()
        else:
            self.app.host.quit()

    def on_resume(self) -> None:
        self._show()

    def handle_key(self, key: str) -> bool:
        if key in ("up", "w", "k"):
            self.menu.move_up()
        elif key in ("down", "s", "j"):
            self.menu.move_down()
        elif key in ("enter", "space"):
            self.menu.confirm()
        elif key == "q":
            self.app.host.quit()
        elif key == "escape":
            self.app.leave()
        else:
            return False
        return True

    def update(self, dt: float) -> None:
        pass

    def render(self) -> None:
        r = self.app.renderer
        r.clear()
        r.draw_rect(0, 0, r.width, r.height, theme.VOID)
        if _too_small(r, theme.MENU_MIN_COLS, theme.MENU_MIN_ROWS):
            r.present()
            return

        cx = r.width // 2
        box_top = self._menu_box_top(r)
        y = _draw_title(r, cx, box_top)
        if y < box_top:
            r.ui_text(cx, y, theme.TAGLINE, fill=theme.HUD_DIM, anchor="n")

        self.menu.render()

        # The IFF key on the title screen, not buried in the rules. It is the
        # one thing a player must know before the first wave, and the web
        # build puts it here for the same reason.
        key_row = r.height - 5
        if key_row > box_top:
            # Laid out from the measured widths rather than from guessed
            # offsets: the first version hardcoded cx-14 and cx+2 and the two
            # halves overlapped into "squawks - esco^ dark", which is the one
            # line on this screen that must not be ambiguous.
            aid = f"{theme.BEACON} squawks — escort"
            hostile = f"{theme.HOSTILE_NEAR} dark — destroy"
            gap = 4
            total = len(aid) + gap + len(hostile)
            if total <= r.width - 2:
                left = cx - total // 2
                r.ui_text(left, key_row, aid, fill=theme.AID)
                r.ui_text(left + len(aid) + gap, key_row, hostile,
                          fill=theme.HOSTILE)
            else:
                # Too narrow for one line, so stack them rather than truncate:
                # a half-drawn IFF key is worse than none.
                r.ui_text(cx, key_row - 1, aid, fill=theme.AID, anchor="n")
                r.ui_text(cx, key_row, hostile, fill=theme.HOSTILE, anchor="n")

        if self.app.best:
            r.ui_text(cx, r.height - 3, f"best: {self.app.best}",
                      fill=theme.HUD_DIM, anchor="n")
        r.ui_text(cx, r.height - 2, _fit(MENU_HELP, r.width - 2),
                  fill=theme.HUD_DIM, anchor="n")
        if self.app.host.seated:
            r.ui_text(cx, r.height - 1, _fit(ARCADE_HELP, r.width - 2),
                      fill=theme.HUD_DIM, anchor="n")
        r.present()

    def _menu_box_top(self, renderer) -> int:
        rows = len(self.ITEMS) * theme.MENU_ITEM_H + theme.MENU_PAD * 2
        return max(0, (renderer.height - rows) // 2)


class RulesScene:
    """The rules, scrolling, because they are longer than a terminal is tall."""

    def __init__(self, app):
        self.app = app
        self.offset = 0

    def _lines(self, width: int) -> list[tuple[str, str]]:
        out: list[tuple[str, str]] = []
        for heading, paragraphs in RULES:
            out.append((heading, "heading"))
            for para in paragraphs:
                for line in textwrap.wrap(para, max(20, width)):
                    out.append((line, "body"))
                out.append(("", "body"))
        return out

    def _viewport(self) -> int:
        return max(1, self.app.renderer.height - 4)

    def _max_offset(self, width: int) -> int:
        return max(0, len(self._lines(width)) - self._viewport())

    def handle_key(self, key: str) -> bool:
        width = max(20, self.app.renderer.width - 6)
        if key in ("up", "w", "k"):
            self.offset = max(0, self.offset - 1)
        elif key in ("down", "s", "j"):
            self.offset = min(self._max_offset(width), self.offset + 1)
        else:
            self.app.host.pop_scene()
        return True

    def update(self, dt: float) -> None:
        pass

    def render(self) -> None:
        r = self.app.renderer
        r.clear()
        r.draw_rect(0, 0, r.width, r.height, theme.VOID)
        if _too_small(r, theme.MENU_MIN_COLS, theme.MENU_MIN_ROWS):
            r.present()
            return

        r.ui_text(2, 0, "HOW TO PLAY", fill=theme.TITLE)
        width = max(20, r.width - 6)
        lines = self._lines(width)
        # Clamped here as well as in handle_key: a resize can shrink the
        # viewport under an offset that was legal a frame ago.
        self.offset = min(self.offset, self._max_offset(width))

        for i in range(self._viewport()):
            index = self.offset + i
            if index >= len(lines):
                break
            text, kind = lines[index]
            fill = theme.AID if kind == "heading" else theme.HUD_TEXT
            r.ui_text(3, 2 + i, _fit(text, r.width - 4), fill=fill)

        more = len(lines) - (self.offset + self._viewport())
        hint = RULES_HELP if (more > 0 or self.offset) else SCORES_HELP
        r.ui_text(2, r.height - 2, _fit(hint, r.width - 2), fill=theme.HUD_DIM)
        r.present()


class InitialsScene:
    """Three letters, for the board."""

    def __init__(self, app, score: int):
        self.app = app
        self.score = score
        self.typed = ""

    def handle_key(self, key: str) -> bool:
        if key == "backspace":
            self.typed = self.typed[:-1]
        elif key == "enter":
            initials = (self.typed or self.app.initials or "AAA").upper()
            self.app.record(self.score, initials)
            self.app.host.pop_scene()
        elif len(key) == 1 and key.isalnum():
            if len(self.typed) < scores.INITIALS_LENGTH:
                self.typed += key.upper()
        else:
            return False
        return True

    def update(self, dt: float) -> None:
        pass

    def render(self) -> None:
        r = self.app.renderer
        r.clear()
        r.draw_rect(0, 0, r.width, r.height, theme.VOID)
        cx, cy = r.width // 2, r.height // 2
        r.ui_text(cx, cy - 3, "THE RAIL REMEMBERS", fill=theme.TITLE, anchor="n")
        r.ui_text(cx, cy - 1, f"score {self.score}", fill=theme.HUD_TEXT, anchor="n")
        shown = self.typed.ljust(scores.INITIALS_LENGTH, "_")
        r.ui_text(cx, cy + 1, " ".join(shown), fill=theme.AID, anchor="n")
        r.ui_text(cx, r.height - 2, _fit(INITIALS_HELP, r.width - 2),
                  fill=theme.HUD_DIM, anchor="n")
        r.present()


class ScoresScene:
    """The board."""

    def __init__(self, app):
        self.app = app
        self.offset = 0

    def _viewport(self) -> int:
        return max(1, self.app.renderer.height - 5)

    def handle_key(self, key: str) -> bool:
        entries = self.app.scores.load()
        if key in ("up", "w", "k"):
            self.offset = max(0, self.offset - 1)
        elif key in ("down", "s", "j"):
            self.offset = min(max(0, len(entries) - self._viewport()),
                              self.offset + 1)
        else:
            self.app.host.pop_scene()
        return True

    def update(self, dt: float) -> None:
        pass

    def render(self) -> None:
        r = self.app.renderer
        r.clear()
        r.draw_rect(0, 0, r.width, r.height, theme.VOID)
        r.ui_text(2, 0, "HIGH SCORES", fill=theme.TITLE)

        entries = self.app.scores.load()
        if not entries:
            r.ui_text(2, 2, "nothing yet", fill=theme.HUD_DIM)
        for i in range(self._viewport()):
            index = self.offset + i
            if index >= len(entries):
                break
            e = entries[index]
            row = f"{index + 1:>3}  {e.initials:<4} {e.score:>8}"
            fill = theme.AID if index == 0 else theme.HUD_TEXT
            r.ui_text(3, 2 + i, _fit(row, r.width - 4), fill=fill)

        more = len(entries) - (self.offset + self._viewport())
        hint = RULES_HELP if (more > 0 or self.offset) else SCORES_HELP
        r.ui_text(2, r.height - 2, _fit(hint, r.width - 2), fill=theme.HUD_DIM)
        r.present()


class GameScene:
    """A run. Real time, thirty frames a second."""

    def __init__(self, app):
        self.app = app
        self.world = World()
        self.entities = Entities()
        self.player = Player()
        self.score = 0
        self.combo = 1
        self.combo_timer = 0.0
        self.strikes = 0
        self.frame = 0
        self.dead = False
        self.paused = False
        self.restart()

    # -- Flow --------------------------------------------------------

    def restart(self) -> None:
        # A run must not inherit the keypress that started it: the engine folds
        # space and enter into the same button, and the Enter that chose FLY
        # THE RAIL would otherwise arrive as the run's first shot.
        self.app.host.input.clear()

        self.world.reset(self.app.rng.random)
        self.entities.reset()
        self.player.reset()
        self.score = 0
        self.combo = 1
        self.combo_timer = 0.0
        self.strikes = 0
        self.frame = 0
        self.dead = False
        self.paused = False
        self.app.last_rank = None

    def _toggle_pause(self) -> None:
        if not self.dead:
            self.paused = not self.paused

    def _end(self) -> None:
        self.dead = True
        if self.app.qualifies(self.score):
            self.app.enter_initials(self.score)
        elif self.score > 0:
            self.app.record(self.score)

    # -- Frame -------------------------------------------------------

    def update(self, dt: float) -> None:
        if self.dead or self.paused:
            return
        self.frame += 1

        source = self.app.host.input
        source.poll()
        # Steering reads held state, which is what hold_ms is for: overshoot is
        # survivable here and the stakes are only which way the ship drifts.
        axis_x = (source.is_pressed("right") or 0) - (source.is_pressed("left") or 0)
        axis_y = (source.is_pressed("down") or 0) - (source.is_pressed("up") or 0)

        t = config.difficulty_at(self.world.distance)
        rail = config.rail_speed(t)

        # Seconds in, 60fps frames out. See FRAMES_PER_SECOND.
        frames = dt * FRAMES_PER_SECOND

        # Firing is not read here. It is an edge, and arrives in handle_key.
        self.player.update(float(axis_x), float(axis_y), False, frames)
        self.world.update(self.player, rail, frames)
        self.entities.update(self.player, rail, t, frames, self.app.rng.random)
        self._score_events()

        if self.combo_timer > 0:
            self.combo_timer -= frames
            if self.combo_timer <= 0:
                self.combo = 1

        if self.player.lives <= 0 or self.strikes >= config.MAX_STRIKES:
            self._end()

    def _score_events(self) -> None:
        for event in self.entities.drain_events():
            if event.type == "hostile-killed":
                self.score += config.SCORE_HOSTILE * self.combo
                self.combo = min(self.combo + 1, config.COMBO_MAX)
                self.combo_timer = config.COMBO_WINDOW
            elif event.type == "aid-escorted":
                self.score += config.SCORE_ESCORT
            elif event.type == "friendly-fire":
                self.score += config.SCORE_FRIENDLY_FIRE
                self.strikes += 1
                self.combo = 1
                self.combo_timer = 0.0
            elif event.type == "aid-lost":
                self.combo = 1
                self.combo_timer = 0.0
            elif event.type == "player-hit":
                self.player.take_hit()

    def handle_key(self, key: str) -> bool:
        if key == "r":
            self.restart()
        elif key == "q":
            self.app.host.quit()
        elif key == "escape":
            self.app.to_title()
        elif key == "p" and not self.dead:
            self._toggle_pause()
        elif self.dead and key in ("enter", "space"):
            self.restart()
        elif key in FIRE_KEYS:
            # The one control whose exact moment is the game. Never read from
            # held state, so that holding the key cannot become a decision the
            # player did not make.
            if not self.dead and not self.paused and self.player.shoot_cooldown <= 0:
                self.player.shoot_cooldown = config.SHOT_COOLDOWN
                self.entities.fire(self.player)
            return True
        else:
            return False
        return True

    # -- Drawing -----------------------------------------------------

    def render(self) -> None:
        r = self.app.renderer
        r.clear()
        r.draw_rect(0, 0, r.width, r.height, theme.VOID)
        if _too_small(r, theme.MIN_COLS, theme.MIN_ROWS):
            r.present()
            return

        rows = r.height - theme.HEADER_ROWS - theme.FOOTER_ROWS
        grid = cells.fit(r.width, rows)
        top = theme.HEADER_ROWS

        self._draw_backdrop(r, grid, top, rows)
        self._draw_deck(r, grid, top, rows)
        self._draw_contacts(r, grid, top, rows)
        self._draw_shots(r, grid, top, rows)
        self._draw_ship(r, grid, top, rows)
        self._draw_header(r)
        self._draw_strip(r)

        if self.dead:
            self._draw_banner(r, "THE RUN IS OVER", theme.WARN)
        elif self.paused:
            self._draw_banner(r, "PAUSED", theme.AID)
        r.present()

    def _place(self, grid, top, rows, canvas_x, canvas_y):
        """A canvas point as a cell, or None when it falls off the frame."""
        col, row = grid.col(canvas_x), top + grid.row(canvas_y)
        if 0 <= col < self.app.renderer.width and top <= row < top + rows:
            return col, row
        return None

    def _draw_backdrop(self, r, grid, top, rows) -> None:
        horizon = top + grid.row(config.HORIZON_Y)
        r.draw_rect(0, top, r.width, max(0, horizon - top), theme.VOID_HAZE)
        for star in self.world.stars:
            at = self._place(grid, top, rows, star.x, star.y)
            if at and at[1] < horizon:
                r.ui_text(at[0], at[1], theme.STAR_GLYPH, fill=theme.STAR)

    def _draw_deck(self, r, grid, top, rows) -> None:
        """The cloud deck: bands receding, and two rails converging.

        The bands are the depth cue — the difference between "contacts are
        growing" and "I am flying at them" is carried almost wholly by them
        streaming past, and a terminal has fewer rows to say it in than a
        canvas does.

        **The rails are not decoration.** ``world.js`` says why: without them,
        sliding left and sliding the whole world right look identical, so the
        camera's lateral drift — which is most of what sells the depth —
        becomes invisible. They cost two thin lines and they are the only thing
        on screen that reports which way you are actually leaning.

        Both are projected at both edges rather than scaled from a centre
        line. The first version multiplied a width by the scale and centred it
        on the window, which quietly ignored the parallax term and pinned the
        deck to the frame while everything else moved against it.
        """
        cam_x, cam_y = self.world.cam_x, self.world.cam_y
        horizon_row = top + grid.row(config.HORIZON_Y)
        bottom = top + rows

        # The ground under the bands, so they read as marks on a surface
        # rather than as lines in the void.
        if horizon_row < bottom:
            r.draw_rect(0, horizon_row, r.width, bottom - horizon_row,
                        theme.DECK_FAR)

        # Near to far, keeping a gap between drawn bands.
        #
        # **This is the one place the terminal cannot simply follow the
        # canvas.** There a band is a one-to-three pixel line in a hundred and
        # fifty pixel deck, so thirty of them still leave mostly gap, and the
        # gap is what the eye reads the motion against. A cell has no fraction
        # of a row: every band is a full row or nothing, and thirty bands
        # across the ten rows below the horizon fills every one of them. The
        # first version did exactly that and produced a wall of tildes with no
        # motion in it at all.
        #
        # So the canvas's alpha-and-thickness ramp becomes row spacing here.
        # Nearest first, and a band is skipped unless it clears the last drawn
        # one, which keeps the duty cycle the canvas gets for free.
        drawn: list[int] = []
        for z in sorted(self.world.bands):
            left = projection.point(-DECK_HALF_W, DECK_Y, z, cam_x, cam_y)
            right = projection.point(DECK_HALF_W, DECK_Y, z, cam_x, cam_y)
            row = top + grid.row(left.y)
            if not (horizon_row < row < bottom):
                continue
            if any(abs(row - other) < DECK_ROW_GAP for other in drawn):
                continue

            c0 = max(0, grid.col(left.x))
            c1 = min(r.width, grid.col(right.x))
            if c1 <= c0:
                continue

            # What is left of the ramp: near bands are heavier and brighter,
            # far ones thin toward the haze.
            t = 1 - z / config.DECK_Z_SPAN
            near = t > 0.55
            glyph = theme.DECK_GLYPH if near else theme.DECK_FAINT
            fill = theme.DECK_NEAR if near else theme.DECK_LINE
            r.ui_text(c0, row, glyph * (c1 - c0), fill=fill)
            drawn.append(row)

        self._draw_rails(r, grid, top, rows, horizon_row)

    def _draw_rails(self, r, grid, top, rows, horizon_row) -> None:
        """Two lines from the vanishing point out to the near corners.

        Walked a row at a time and interpolated, because a cell grid has no
        line primitive and stepping by column would draw a dotted rail wherever
        the slope is steeper than one cell per column.
        """
        cam_x, cam_y = self.world.cam_x, self.world.cam_y
        vx, vy = projection.vanishing(cam_x, cam_y)
        v_col, v_row = grid.col(vx), top + grid.row(vy)
        bottom = top + rows

        for side in (-1, 1):
            near = projection.point(side * RAIL_NEAR_X, DECK_Y, RAIL_NEAR_Z,
                                    cam_x, cam_y)
            n_col, n_row = grid.col(near.x), top + grid.row(near.y)
            if n_row <= v_row:
                continue
            for row in range(max(horizon_row, v_row), min(bottom, n_row + 1)):
                fraction = (row - v_row) / (n_row - v_row)
                col = int(v_col + (n_col - v_col) * fraction)
                if 0 <= col < r.width:
                    r.ui_text(col, row, theme.RAIL_GLYPH, fill=theme.DECK_LINE)

    def _draw_contacts(self, r, grid, top, rows) -> None:
        """Far to near, so a near contact draws over a far one.

        The transponder goes beside the hull rather than on it: one cell each,
        so the blink cannot be mistaken for the contact changing shape, and so
        a lit convoy is two cells wide where a hostile is one.
        """
        for c in sorted(self.entities.contacts, key=lambda c: -c.z):
            p = projection.point(c.x, c.y, c.z, self.world.cam_x, self.world.cam_y)
            at = self._place(grid, top, rows, p.x, p.y)
            if not at:
                continue
            col, row = at
            dim = c.z > config.Z_FIRE_MAX
            r.ui_text(col, row,
                      theme.contact_glyph(c.kind, c.z, config.Z_SHAPE_READABLE),
                      fill=theme.contact_colour(c.kind, dim=dim))
            if self.entities.beacon_lit(c, self.frame) and col + 1 < r.width:
                r.ui_text(col + 1, row, theme.BEACON, fill=theme.AID_BEACON)
            # A convoy under fire, marked so the rescue is possible.
            if c.doom_timer > 0 and col > 0:
                r.ui_text(col - 1, row, "!", fill=theme.WARN)

    def _draw_shots(self, r, grid, top, rows) -> None:
        for s in self.entities.shots:
            p = projection.point(s.x, s.y, s.z, self.world.cam_x, self.world.cam_y)
            at = self._place(grid, top, rows, p.x, p.y)
            if at:
                r.ui_text(at[0], at[1], theme.SHOT_GLYPH, fill=theme.SHOT)

    def _draw_ship(self, r, grid, top, rows) -> None:
        # Blink through the invincibility window, slowly enough to read as a
        # state rather than as a rendering fault.
        if self.player.invincible > 0 and int(self.player.invincible / 5) % 2 == 0:
            return
        p = projection.point(self.player.x, self.player.y, 0,
                             self.world.cam_x, self.world.cam_y)
        at = self._place(grid, top, rows, p.x, p.y)
        if not at:
            return
        col, row = at
        glyph = theme.SHIP
        start = max(0, col - len(glyph) // 2)
        r.ui_text(start, row, glyph[:max(0, r.width - start)], fill=theme.SHIP_HULL)

    def _draw_header(self, r) -> None:
        r.ui_text(1, 0, f"SCORE {self.score}", fill=theme.HUD_TEXT)
        if self.combo > 1:
            r.ui_text(14, 0, f"x{self.combo}", fill=theme.AID)
        lives = theme.LIFE_GLYPH * max(0, self.player.lives)
        strikes = theme.STRIKE_GLYPH * self.strikes
        right = f"{lives}  {strikes}"
        r.ui_text(max(0, r.width - len(right) - 1), 0, right, fill=theme.LIFE_ICON)

    def _draw_strip(self, r) -> None:
        """The second identification channel: every contact, and its depth.

        Drawn from :meth:`Entities.list_contacts` so a cluttered frame never
        hides the answer. In a terminal this carries more of the load than it
        does on a canvas — depth is a number here, where on the canvas it is a
        size — which is why it gets a row of its own rather than sharing one.
        """
        row = r.height - 2
        parts = []
        for c in self.entities.list_contacts()[:6]:
            mark = theme.BEACON if c.kind == "aid" else theme.HOSTILE_FAR
            parts.append(f"{mark}{int(max(0, c.z)):>4}")
        r.ui_text(1, row, _fit("  ".join(parts), r.width - 2), fill=theme.HUD_DIM)
        r.ui_text(1, r.height - 1, _fit(GAME_HELP, r.width - 2), fill=theme.HUD_DIM)

    def _draw_banner(self, r, text: str, fill: str) -> None:
        cx, cy = r.width // 2, r.height // 2
        r.ui_text(cx, cy - 1, _fit(text, r.width - 2), fill=fill, anchor="n")
        r.ui_text(cx, cy, _fit(f"score {self.score}", r.width - 2),
                  fill=theme.HUD_TEXT, anchor="n")
        hint = ("Enter to fly again    Esc for the title" if self.dead
                else "P to fly on    Esc for the title")
        r.ui_text(cx, cy + 1, _fit(hint, r.width - 2), fill=theme.HUD_DIM,
                  anchor="n")


__all__ = [
    "ARCADE_HELP", "FIRE_KEYS", "GAME_HELP", "MENU_HELP", "RULES",
    "GameScene", "InitialsScene", "RulesScene", "ScoresScene", "TitleScene",
]
