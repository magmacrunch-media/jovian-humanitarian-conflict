"""The canvas onto character cells, and the depth finding it settled.

No engine and no oracle: this step exists only in the terminal version, so
there is no JavaScript to check it against. What can be checked is that it
never hides anything, that it keeps the frame's shape, and that the reasoning
recorded in the module docstring is still true of the numbers.
"""

from itertools import pairwise

import pytest

from jovian import cells, config, projection


def test_the_whole_frame_is_always_visible():
    """Fit, never crop. A contact off the edge is a contact you cannot
    identify, and identification is the entire game."""
    for cols, rows in ((60, 18), (80, 22), (100, 28), (120, 38), (200, 50),
                       (40, 40), (300, 10)):
        m = cells.fit(cols, rows)
        for x, y in ((0, 0), (config.CANVAS_W, config.CANVAS_H),
                     (0, config.CANVAS_H), (config.CANVAS_W, 0)):
            col, row = m.col(x), m.row(y)
            assert -1 <= col <= cols, f"{cols}x{rows}: corner col {col}"
            assert -1 <= row <= rows, f"{cols}x{rows}: corner row {row}"


def test_the_frame_keeps_its_shape():
    """One scale for both axes, up to the cell aspect. A frame stretched to
    fill the window would put the perspective divide at odds with itself."""
    for cols, rows in ((60, 18), (80, 22), (100, 28), (160, 45)):
        m = cells.fit(cols, rows)
        assert m.scale_x / m.scale_y == pytest.approx(cells.CELL_ASPECT)


def test_a_standard_window_needs_no_letterbox_worth_mentioning():
    """80x24 with a header and a footer is 80x22, and the canvas aspect times
    the cell aspect is 3.56 — which is what a terminal already nearly is."""
    m = cells.fit(80, 22)
    assert m.origin_col == pytest.approx(0.9, abs=0.2)
    assert m.origin_row == pytest.approx(0.0, abs=0.2)


def test_a_degenerate_region_draws_nothing_rather_than_dividing_by_zero():
    for cols, rows in ((0, 22), (80, 0), (-5, -5)):
        m = cells.fit(cols, rows)
        assert not m.valid
        assert not m.on_grid(0, 0)


def test_depth_reads_horizontally_not_vertically():
    """The finding this module was written to settle, pinned.

    A contact's row is its height in the band, and height and depth are
    conflated — the vanishing point guarantees a contact at the camera's own y
    lands on the horizon row at every depth. Convergence in x is what actually
    separates depths, and on an 80x22 playfield it has roughly five times the
    room.

    If a future change to FOCAL or the layout inverts this, the scenes are
    drawing depth with the wrong axis and should be told so here rather than
    discovered by squinting at a terminal.
    """
    m = cells.fit(80, 22)
    rows, cols_ = set(), set()
    for z in (1100, 800, 600, 410, 200, 60, 0):
        rows.add(m.row(projection.point(0, config.CONTACT_Y_SPREAD, z, 0, 0).y))
        cols_.add(m.col(projection.point(config.SPAWN_X_RANGE, 0, z, 0, 0).x))

    row_span = max(rows) - min(rows) + 1
    col_span = max(cols_) - min(cols_) + 1
    assert col_span > row_span * 3, (
        f"depth spans {col_span} columns and {row_span} rows; the scenes "
        f"assume the horizontal axis carries it"
    )


def test_the_horizon_row_is_the_same_at_every_depth():
    """Stated as the reason row position cannot carry depth, so worth being a
    test rather than a claim in a docstring."""
    m = cells.fit(80, 22)
    landed = {m.row(projection.point(0, 0, z, 0, 0).y)
              for z in (0, 200, 410, 600, 800, 1100, 3000)}
    assert len(landed) == 1


def test_convergence_is_monotonic():
    """Nearer is always wider. A non-monotonic mapping would let a near contact
    read as further away than a far one, which in this game hides the answer."""
    m = cells.fit(80, 22)
    offsets = [m.col(projection.point(config.SPAWN_X_RANGE, 0, z, 0, 0).x)
               for z in (1100, 800, 600, 410, 200, 60, 0)]
    assert all(a <= b for a, b in pairwise(offsets))
