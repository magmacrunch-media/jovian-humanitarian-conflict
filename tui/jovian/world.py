"""The rail itself: camera drift, the receding cloud deck, stars.

Ported from ``web/js/world.js``, minus everything below its drawing line.

The world holds no geometry that anything collides with — it is entirely depth
cueing. That is not decoration, and it matters more in a terminal than on a
canvas: at 480x270 with a hyperbolic scale, the difference between "ships are
growing" and "I am flying at them" is carried almost wholly by the deck bands
streaming past. A character grid has fewer rows to spend on that difference, so
the bands are the first thing the cell mapping will have to get right.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass, field

from jovian import config

#: Deck bands in the recycling ring. A literal in ``world.js`` and kept as one,
#: so the two files stay comparable line for line.
BAND_COUNT = 30

#: Stars on the backdrop.
STAR_COUNT = 46

#: How much of the ship's vertical position the camera follows. Half, so
#: climbing lifts the horizon less than it lifts the ship — which is what keeps
#: the ship readable against the deck instead of pinned to it.
CAM_FOLLOW_Y = 0.5

#: Star brightness, as a base and a spread.
STAR_MIN_BRIGHTNESS = 0.25
STAR_BRIGHTNESS_SPREAD = 0.6


@dataclass
class Star:
    """Fixed to the backdrop, not the rail.

    Stars are meant to read as infinitely far away, so they respond to camera
    drift and to nothing else — they never recede and never wrap.
    """

    x: float
    y: float
    brightness: float


@dataclass
class World:
    distance: float = 0.0
    cam_x: float = 0.0
    cam_y: float = 0.0
    bands: list[float] = field(default_factory=list)
    stars: list[Star] = field(default_factory=list)

    def reset(self, random: Callable[[], float]) -> None:
        """Rebuild the rail.

        ``random`` is required rather than defaulting to :mod:`random`, so a
        run is reproducible from a seed and a test never depends on global
        state. The web build calls ``Math.random`` directly because a browser
        has no seeded run to protect; every other injected-random in this port
        is the same divergence for the same reason.
        """
        self.distance = 0.0
        self.cam_x = 0.0
        self.cam_y = 0.0

        # Deck bands recycle rather than being spawned and culled: a fixed ring
        # of depths, each wrapping back as it passes the camera. Constant
        # memory, no allocation in the loop, and the spacing cannot drift.
        self.bands = [config.DECK_Z_SPAN * (i / BAND_COUNT)
                      for i in range(BAND_COUNT)]

        self.stars = [
            Star(
                x=random() * config.CANVAS_W,
                y=random() * config.HORIZON_Y,
                brightness=STAR_MIN_BRIGHTNESS + random() * STAR_BRIGHTNESS_SPREAD,
            )
            for _ in range(STAR_COUNT)
        ]

    def update(self, player, rail_speed: float, dt: float) -> None:
        """Advance the rail one frame.

        The camera trails the ship by a fraction of the gap per frame. Because
        that is a proportional chase rather than a fixed step, it has to be
        raised to ``dt`` — a straight multiply overshoots badly at low frame
        rates and the horizon visibly wobbles.
        """
        self.distance += rail_speed * dt

        follow_x = player.x
        follow_y = player.y * CAM_FOLLOW_Y
        k = 1 - (1 - config.CAM_LAG) ** dt
        self.cam_x += (follow_x - self.cam_x) * k
        self.cam_y += (follow_y - self.cam_y) * k

        for i in range(len(self.bands)):
            self.bands[i] -= rail_speed * dt
            # Wrap by adding the span rather than assigning it, so the even
            # spacing survives a large dt instead of collapsing into a clump.
            while self.bands[i] <= 0:
                self.bands[i] += config.DECK_Z_SPAN


__all__ = [
    "BAND_COUNT", "CAM_FOLLOW_Y", "STAR_BRIGHTNESS_SPREAD",
    "STAR_MIN_BRIGHTNESS", "STAR_COUNT", "Star", "World",
]
