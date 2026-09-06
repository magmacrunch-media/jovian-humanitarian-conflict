"""What the arcade needs to know to launch this game.

The entry point a launcher resolves. Kept tiny and free of imports that cost
anything: a menu listing several games loads one of these per installed game
just to draw a row, and it should not pay for the game's rules or its screens
to do that.
"""

from __future__ import annotations

from typing import Any

from magmacrunch.engine.arcade import GameInfo

from jovian import theme

INFO = GameInfo(
    key="jovian-humanitarian-conflict",
    title="The Jovian Humanitarian Conflict",
    blurb="Know what you are shooting at. Escort the convoys, kill the rest.",
    # 30 fps because the rail moves whether or not a key was pressed, and 20 is
    # visibly steppy once contacts are converging.
    fps=30,
    # **The first cabinet here to want this above zero**, and the reason is the
    # seam TuiInput's own docstring draws: decay semantics suit a held
    # *direction*, where overshoot is survivable and the stakes are which way
    # something drifts. Steering is exactly that — it feeds an
    # accelerate-and-coast model with drag, and the web build's touch backend
    # already proves the axis tolerates going quiet, since a lifted thumb
    # coasts rather than snapping to centre.
    #
    # 180ms clears a typical repeat delay without leaving the ship steering
    # itself for long after a real release. Moonlight Drift documented at
    # length why it could NOT use this: thrust there is a control whose exact
    # timing is the game. Here that description fits the other verb — firing —
    # which is why firing is an edge read in handle_key and never from held
    # state, and the two live side by side without fighting.
    hold_ms=180,
    min_cols=theme.MIN_COLS,
    min_rows=theme.MIN_ROWS,
)


class JovianGame:
    """Satisfies :class:`magmacrunch.engine.arcade.ArcadeGame`."""

    info = INFO

    def start(self, host: Any) -> Any:
        """The title screen, ready to be pushed.

        Imported here rather than at module scope so that listing this game in
        an arcade menu does not drag in its rules, its screens, or Textual.
        """
        from jovian.app import JovianApp

        return JovianApp(host).root_scene


#: What the entry point resolves to. Stateless — a run's state belongs to the
#: JovianApp that :meth:`JovianGame.start` creates, so replaying makes a new one.
GAME = JovianGame()

__all__ = ["GAME", "INFO", "JovianGame"]
