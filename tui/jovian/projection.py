"""The pseudo-3D transform. Pure maths: no engine, no terminal, no state.

Transcribed from ``web/js/projection.js``, which keeps this in its own file for
a reason that carries over unchanged: three unrelated things depend on it
agreeing with itself — the world draws its cloud deck through it, entities draw
contacts through it, and collision converts the ship's box back into world
units at a target's depth. A transform that disagreed between drawing and
hitting would produce shots that visibly connect and do nothing, which is the
least debuggable class of bug in a shooter.

Being pure, it is also the one part that can be asserted outright rather than
eyeballed, which is why it is written first and tested hardest.

**This maps world space to the 480x270 canvas, not to character cells.** The
cell mapping is a second step on top of it, and lives elsewhere for the same
reason :mod:`jovian.config` refuses to name a terminal size: two coordinate
spaces in one module get mixed. Everything here is in canvas units and is
byte-identical to the browser.
"""

from __future__ import annotations

from typing import NamedTuple

from jovian import config


class Point(NamedTuple):
    """A projected point, and the scale it was projected at.

    The scale comes back alongside the coordinates because every caller needs
    it to size whatever it is about to draw. Recomputing it is both wasteful
    and a chance for the two to drift apart.
    """

    x: float
    y: float
    s: float


def scale_at(z: float) -> float:
    """Perspective divide.

    Exactly 1.0 at the player's own plane and falling off hyperbolically with
    depth, so distant things bunch toward the horizon the way a real lens makes
    them. Monotonically decreasing for all ``z > -FOCAL``, which is what lets
    draw order be a plain sort on z.
    """
    return config.FOCAL / (z + config.FOCAL)


def depth_at(s: float) -> float:
    """Inverse of :func:`scale_at`: the depth at which the world draws at ``s``.

    Used to size the cloud deck's bands from the screen rows they should land
    on, rather than hand-tuning depths until the spacing looks right.
    """
    return config.FOCAL / s - config.FOCAL


def point(wx: float, wy: float, wz: float, cam_x: float, cam_y: float) -> Point:
    """World point to screen point.

    ``cam_x`` / ``cam_y`` are the camera's drift, which trails the ship. The
    world shifts by ``-cam * scale`` (ordinary parallax: near things slide
    further than far ones) while the vanishing point itself slides the *other*
    way by ``cam * PARALLAX``. That second term is what sells the bank —
    leaning left swings the whole horizon right, and without it the rail reads
    as a flat scrolling backdrop no matter how correct the divide is.
    """
    s = scale_at(wz)
    return Point(
        x=config.CANVAS_W / 2 + (wx - cam_x) * s + cam_x * config.PARALLAX,
        y=config.HORIZON_Y + (wy - cam_y) * s + cam_y * config.PARALLAX,
        s=s,
    )


def vanishing(cam_x: float, cam_y: float) -> tuple[float, float]:
    """Where the vanishing point currently sits on screen.

    ``point(cam_x, cam_y, z)`` lands here for every ``z`` — that is the
    definition of a vanishing point, and the tests assert it as one rather than
    trusting the arithmetic.
    """
    return (
        config.CANVAS_W / 2 + cam_x * config.PARALLAX,
        config.HORIZON_Y + cam_y * config.PARALLAX,
    )


def in_box(shot_x: float, shot_y: float, wx: float, wy: float,
           half_w: float, half_h: float) -> bool:
    """Does a shot fired from ``(shot_x, shot_y)`` pass through a contact's box?

    Shots run parallel to the z axis from the ship's plane, so this is a 2D
    test in the ``z = 0`` frame and does not involve the screen at all. Doing
    it in world space is what keeps aiming honest at every depth: a target that
    looks centred under the reticle *is* centred, rather than being easier to
    hit up close because its sprite is bigger.

    A box rather than a radius because every sprite here is wider than it is
    tall. One circle sized to the width reaches far above and below a hull that
    is not there; sized to the height it misses the wingtips. Both were wrong
    in the same frame.
    """
    return abs(wx - shot_x) <= half_w and abs(wy - shot_y) <= half_h


__all__ = ["Point", "depth_at", "in_box", "point", "scale_at", "vanishing"]
