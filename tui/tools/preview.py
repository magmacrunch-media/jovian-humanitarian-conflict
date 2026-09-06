"""Draw a frame, so the depth question is answered by looking at it.

Throwaway, and deliberately so: it exists to settle whether row position alone
can carry depth on a character grid before any scene code is written against
the assumption that it can. Run it, read it, then decide.

    python tools/preview.py

Nothing imports this and nothing tests it.
"""

from __future__ import annotations

import sys

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent.parent))

from jovian import cells, config, projection  # noqa: E402
from jovian.entities import Contact, Entities  # noqa: E402

# Contacts laid along the whole rail, alternating kind, so the question is
# what a row of depths looks like rather than what one contact looks like.
DEPTHS = [1100, 950, 800, 660, 530, 410, 300, 200, 120, 60, 10]


def scene() -> list[Contact]:
    out = []
    for i, z in enumerate(DEPTHS):
        out.append(Contact(
            id=i + 1,
            kind="aid" if i % 3 == 1 else "hostile",
            # Spread laterally so they do not stack in one column.
            x=(i - len(DEPTHS) / 2) * 26,
            y=((i % 5) - 2) * 16,
            z=float(z),
            phase=0.0,
            drift_seed=1.0,
        ))
    return out


def draw(contacts, cols, rows, frame, depth_glyphs: bool, dim: bool) -> str:
    grid = [[" "] * cols for _ in range(rows)]
    m = cells.fit(cols, rows)
    e = Entities()

    # The horizon, so the frame has somewhere to recede to.
    hr = m.row(config.HORIZON_Y)
    if 0 <= hr < rows:
        grid[hr] = list("-" * cols)

    for c in sorted(contacts, key=lambda c: -c.z):
        p = projection.point(c.x, c.y, c.z, 0, 0)
        col, row = m.col(p.x), m.row(p.y)
        if not m.on_grid(col, row):
            continue

        near = c.z < config.Z_SHAPE_READABLE
        if depth_glyphs:
            body = ("#" if near else "+") if c.kind == "aid" else ("A" if near else "^")
        else:
            body = "+" if c.kind == "aid" else "^"
        if dim and c.z > config.Z_FIRE_MAX:
            body = body.lower() if body.isalpha() else "."

        grid[row][col] = body
        # The transponder: one cell beside the hull, lit on the double-tap.
        if e.beacon_lit(c, frame) and col + 1 < cols:
            grid[row][col + 1] = "*"

    return "\n".join("".join(r).rstrip() for r in grid)


def show(title, cols, rows, **kw):
    play = rows - 2
    print(f"\n{title}  ({cols}x{rows}, playfield {cols}x{play})")
    print("+" + "-" * cols + "+")
    for line in draw(scene(), cols, play, frame=2, **kw).split("\n"):
        print("|" + line.ljust(cols) + "|")
    print("+" + "-" * cols + "+")


def main() -> None:
    m = cells.fit(80, 22)
    print("fit(80, 22):")
    print(f"  scale {m.scale_x:.4f} x {m.scale_y:.4f}   "
          f"origin {m.origin_col:.1f}, {m.origin_row:.1f}")
    print("\nrows each depth lands on, camera centred:")
    for z in DEPTHS:
        p = projection.point(0, 0, z, 0, 0)
        print(f"  z={z:>5}  scale {projection.scale_at(z):.3f}  "
              f"canvas y {p.y:6.1f}  row {m.row(p.y)}")

    show("A. position only", 80, 24, depth_glyphs=False, dim=False)
    show("B. position + silhouette past Z_SHAPE_READABLE", 80, 24,
         depth_glyphs=True, dim=False)
    show("C. position + silhouette + dim beyond firing range", 80, 24,
         depth_glyphs=True, dim=True)
    show("C, on a small terminal", 60, 20, depth_glyphs=True, dim=True)


main()
