"""The 480x270 canvas onto character cells.

The second and last coordinate step. :mod:`jovian.projection` maps world space
onto the canvas and is byte-identical to the browser; this maps that canvas
onto whatever the terminal happens to be, and exists only here. Keeping them
apart is what let the first one be checked against the shipped JavaScript at
all — mixing a terminal size into it would have made every comparison
untestable.

**A character cell is not square, and that is the whole problem.** A cell is
about twice as tall as it is wide in every terminal font worth naming, so a
frame that is 480x270 in canvas pixels wants a grid whose columns outnumber its
rows by ``(480 / 270) * 2``, which is 3.56 to one. That is close to the shape
terminals already are: 80 columns wants 22 rows, and a standard 80x24 window
with a header and a footer leaves exactly that.

**Fit, never crop.** Moonlight Drift fills vertically and lets the width run
off the sides, which is right for a game where the world is wider than the
window and scrolling past it is the point. It would be wrong here. Every
contact must be identifiable, and a contact cropped off the edge is one you
cannot identify — so this letterboxes instead, showing the entire frame and
leaving blank cells where the aspect does not divide evenly. Losing two rows to
a margin costs nothing; losing a convoy off the left edge costs a run.

**Depth reads horizontally.** The open question this module was written to
settle was whether a row can carry depth, and it cannot: a contact's row is its
height in the band, and height and depth are conflated — a contact on the
horizon row may be at any depth at all, which is the vanishing point doing
exactly what it is supposed to. Measured across the spawn band on an 80x22
playfield, the whole rail from spawn to camera moves a contact through **three
rows and seventeen columns**.

So the grid being landscape, which is usually the constraint a terminal port
fights, is here the asset. Perspective convergence in x is the primary depth
cue and there is plenty of room for it; the cell aspect doubles that advantage
again, since a cell covers half as much canvas horizontally as vertically. Row
position is left to say what it says on the canvas — where in the band a
contact sits — and depth is carried by convergence, by silhouette from
``Z_SHAPE_READABLE`` inward, and by the HUD strip, which can simply print it.
"""

from __future__ import annotations

from dataclasses import dataclass

from jovian import config

#: How much taller than wide a character cell is. Two is right for essentially
#: every terminal font; a constant rather than a measurement because nothing in
#: a terminal will tell you the real number.
CELL_ASPECT = 2.0


@dataclass(frozen=True)
class Cells:
    """Where the canvas lands on the grid."""

    #: Columns per canvas pixel, and rows per canvas pixel. ``scale_x`` is the
    #: larger by :data:`CELL_ASPECT`, because a cell covers more canvas
    #: horizontally than vertically.
    scale_x: float
    scale_y: float
    #: Top-left of the drawn frame, in cells. Non-zero where the letterbox
    #: leaves a margin.
    origin_col: float
    origin_row: float
    #: The region this was fitted into.
    cols: int
    rows: int
    #: False when the region was degenerate; the caller draws nothing rather
    #: than dividing by zero.
    valid: bool = True

    def col(self, canvas_x: float) -> int:
        return int(self.origin_col + canvas_x * self.scale_x)

    def row(self, canvas_y: float) -> int:
        return int(self.origin_row + canvas_y * self.scale_y)

    def on_grid(self, col: int, row: int) -> bool:
        return 0 <= col < self.cols and 0 <= row < self.rows


def fit(cols: int, rows: int) -> Cells:
    """The mapping that shows the whole canvas inside a ``cols`` x ``rows`` region.

    ``rows`` is the *playfield*, not the window: the HUD is the game's second
    identification channel and must not be drawn over the frame it explains.
    """
    if cols <= 0 or rows <= 0:
        return Cells(1.0, 1.0, 0.0, 0.0, max(cols, 0), max(rows, 0), valid=False)

    # One scale for both axes, in canvas pixels per cell-of-the-right-shape.
    # Taking the smaller of the two candidates is what makes this a fit rather
    # than a crop.
    scale_x = cols / config.CANVAS_W
    scale_y = rows / config.CANVAS_H
    if scale_x / CELL_ASPECT < scale_y:
        scale_y = scale_x / CELL_ASPECT
    else:
        scale_x = scale_y * CELL_ASPECT

    drawn_w = config.CANVAS_W * scale_x
    drawn_h = config.CANVAS_H * scale_y
    return Cells(
        scale_x=scale_x,
        scale_y=scale_y,
        origin_col=(cols - drawn_w) / 2,
        origin_row=(rows - drawn_h) / 2,
        cols=cols,
        rows=rows,
    )


__all__ = ["CELL_ASPECT", "Cells", "fit"]
