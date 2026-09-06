"""The ship: flight, banking, guns, invincibility.

Ported from ``web/js/player.js``, minus its ``draw`` and ``drawReticle`` — a
terminal draws neither the way a canvas does, and the file's own comment marks
that line as the boundary the simulation must stay above.

The ship never leaves the ``z = 0`` plane, so its world x/y are screen offsets
from the vanishing point and need no projection. It is the one thing in the
game drawn without going through :mod:`jovian.projection`, and everything else
is drawn relative to it.
"""

from __future__ import annotations

from typing import NamedTuple

from jovian import config

#: How fast the bank chases lateral speed. Not in the web build's CONFIG
#: either — it is a literal in ``player.js`` and is kept as one here rather
#: than promoted, so the two files stay comparable line for line.
BANK_CHASE = 0.18


class Muzzle(NamedTuple):
    x: float
    y: float


class Player:
    """Satisfies nothing in particular: it is plain state and one update.

    ``update`` takes its input as arguments rather than reading a device,
    which is what lets the headless tests fly the ship across the whole input
    space without a keyboard. The web build does the same, for the same
    reason, and it is the seam a terminal plugs into unchanged.
    """

    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self.x: float = 0.0
        self.y: float = float(config.SHIP_Y_START)
        self.vx: float = 0.0
        self.vy: float = 0.0
        #: -1..1, lags vx so the roll reads as inertia.
        self.bank: float = 0.0
        self.shoot_cooldown: float = 0.0
        self.invincible: float = 0.0
        self.lives: int = config.MAX_LIVES
        self.thrust_phase: float = 0.0

    def update(self, axis_x: float, axis_y: float, wants_fire: bool,
               dt: float) -> bool:
        """One frame of flight. True on the frames a shot is actually fired."""
        # Accelerate toward the stick, then bleed off. Drag is a per-frame
        # multiplier, so it is raised to dt rather than multiplied by it — the
        # difference is invisible at 60Hz and a third of the top speed at
        # 144Hz.
        self.vx += axis_x * config.SHIP_ACCEL * dt
        self.vy += axis_y * config.SHIP_ACCEL * dt
        drag = config.SHIP_DRAG ** dt
        self.vx *= drag
        self.vy *= drag

        top = config.SHIP_SPEED_MAX
        self.vx = max(-top, min(top, self.vx))
        self.vy = max(-top, min(top, self.vy))

        self.x += self.vx * dt
        self.y += self.vy * dt

        # Hard walls rather than a wrap: the rail has edges, and a ship that
        # reappeared on the far side would break the parallax it is driving.
        # Zeroing the velocity on contact stops it creeping while held.
        if self.x < -config.SHIP_X_RANGE:
            self.x, self.vx = -float(config.SHIP_X_RANGE), 0.0
        if self.x > config.SHIP_X_RANGE:
            self.x, self.vx = float(config.SHIP_X_RANGE), 0.0
        if self.y < config.SHIP_Y_MIN:
            self.y, self.vy = float(config.SHIP_Y_MIN), 0.0
        if self.y > config.SHIP_Y_MAX:
            self.y, self.vy = float(config.SHIP_Y_MAX), 0.0

        # Bank chases the lateral speed rather than the stick, so it settles a
        # beat after you stop turning instead of snapping flat.
        target_bank = (self.vx / top) * config.BANK_MAX
        self.bank += (target_bank - self.bank) * min(1.0, BANK_CHASE * dt)

        self.thrust_phase += dt
        if self.invincible > 0:
            self.invincible -= dt
        if self.shoot_cooldown > 0:
            self.shoot_cooldown -= dt

        if wants_fire and self.shoot_cooldown <= 0:
            self.shoot_cooldown = config.SHOT_COOLDOWN
            return True
        return False

    def muzzle(self) -> Muzzle:
        """Where a shot leaves the ship. The nose, not the centre."""
        return Muzzle(self.x, self.y - 2)

    def can_be_hit(self) -> bool:
        return self.invincible <= 0

    def take_hit(self) -> bool:
        """True if the hit actually landed, i.e. was not inside i-frames."""
        if not self.can_be_hit():
            return False
        self.lives -= 1
        self.invincible = config.INVINCIBLE_FRAMES
        return True


__all__ = ["BANK_CHASE", "Muzzle", "Player"]
