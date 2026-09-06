"""Wiring — what outlives a single run, and how the screens reach it.

Everything that draws lives in :mod:`jovian.scenes`; everything that decides
lives in :mod:`jovian.config`, :mod:`jovian.player`, :mod:`jovian.entities`,
:mod:`jovian.world` and :mod:`jovian.projection`. This holds them together.

**The terminal is not owned here.** It belongs to a
:class:`~magmacrunch.engine.core.tui_host.TuiHost`, which this is handed — the
same arrangement the other three cabinets use, and what lets the game run as
its own command *and* be seated by a launcher without knowing which happened.
"""

from __future__ import annotations

import random
from typing import Any

from magmacrunch.engine.scores import ScoreBook

from jovian.scenes import GameScene, TitleScene


class JovianApp:
    """A session, drawing on somebody else's terminal."""

    #: The key the browser build posts under, and therefore the key this files
    #: its local board under too.
    SCORE_KEY = "jovian-humanitarian-conflict"

    def __init__(self, host: Any, seed: int | None = None,
                 scores: ScoreBook | None = None):
        self.host = host
        #: Seeded so a run can be replayed. Every module that consumes
        #: randomness takes it as an argument rather than reaching for the
        #: global one, which is what makes that possible.
        self.rng = random.Random(seed)
        self.scores = scores or ScoreBook(self.SCORE_KEY)
        self.initials = "AAA"
        self.last_rank: int | None = None
        self.root_scene = TitleScene(self)

    # -- What the scenes reach for -----------------------------------

    @property
    def renderer(self):
        return self.host.renderer

    @property
    def best(self) -> int:
        return self.scores.best()

    # -- Flow --------------------------------------------------------

    def start_run(self) -> None:
        self.host.push_scene(GameScene(self))

    def show_rules(self) -> None:
        from jovian.scenes import RulesScene

        self.host.push_scene(RulesScene(self))

    def show_scores(self) -> None:
        from jovian.scenes import ScoresScene

        self.host.push_scene(ScoresScene(self))

    def enter_initials(self, score: int) -> None:
        from jovian.scenes import InitialsScene

        self.host.push_scene(InitialsScene(self, score))

    def to_title(self) -> None:
        """Leave the run, keeping the title screen underneath."""
        self.host.pop_scene()

    def leave(self) -> None:
        """Leave the game, not the process.

        Run on its own the title is the last scene and the session ends; under
        a launcher the arcade menu is underneath and this returns to it. Same
        call either way, which is the point of the host owning the stack.
        """
        self.host.pop_scene()

    # -- Scores ------------------------------------------------------

    def qualifies(self, score: int) -> bool:
        return score > 0 and self.scores.qualifies(score)

    def record(self, score: int, initials: str | None = None) -> None:
        if initials:
            self.initials = initials
        result = self.scores.save(self.initials, score)
        self.last_rank = getattr(result, "rank", None)

    @property
    def in_game(self) -> bool:
        return isinstance(self.host.scene, GameScene)


def run(seed: int | None = None, play: bool = False) -> None:
    """The standalone command: build a host, seat this game on it, run it."""
    from magmacrunch.engine.core.tui_host import TuiHost

    from jovian.arcade import GAME

    host = TuiHost(title=GAME.info.title, fps=GAME.info.fps,
                   hold_ms=GAME.info.hold_ms)
    app = JovianApp(host, seed=seed)
    host.push_scene(app.root_scene)
    if play:
        app.start_run()
    host.run()


__all__ = ["JovianApp", "run"]
