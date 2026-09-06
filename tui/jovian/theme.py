"""Palette, layout and glyphs — everything in character cells.

Split out so :mod:`jovian.scenes` and :mod:`jovian.app` can share it without
importing each other. Nothing here imports the engine.

The palette is the web build's ``COLORS`` from ``js/config.js``. Its own
comment records the constraint that matters most here: aid amber against
hostile magenta, *"never red/green, and the two differ in luminance as well as
hue so the distinction survives being read in greyscale."* That is why this
game needs no special handling for a monochrome terminal — it was designed for
one without knowing it.

**Every measurement is cells, not canvas pixels.** The canvas is 480x270 and
lives in :mod:`jovian.config`; :mod:`jovian.cells` maps between them. The two
spaces are kept in separate modules on purpose.
"""

from __future__ import annotations

# ── Palette ─────────────────────────────────────────────────────────
# Transcribed from web/js/config.js.

VOID = "#05060f"
VOID_HAZE = "#0d1230"
STAR = "#c8d4ff"

#: The gas giant's bands, light to dark. A terminal draws the backdrop as flat
#: rows rather than a gradient, so these are the rows it picks from.
BAND_CREAM = "#e8cfa0"
BAND_TAN = "#c99a63"
BAND_UMBER = "#8f5f3c"
BAND_SHADOW = "#5c3a28"
BAND_DEEP = "#38222a"
BANDS = (BAND_CREAM, BAND_TAN, BAND_UMBER, BAND_SHADOW, BAND_DEEP)

DECK_NEAR = "#7a5a7e"
DECK_FAR = "#2a2340"
DECK_LINE = "#b489c4"

HOSTILE = "#ff2fa8"
HOSTILE_DARK = "#7a1150"
AID = "#ffc247"
AID_PALE = "#fff2cf"
AID_DARK = "#8a5f12"
#: The transponder itself. White, so it is the brightest thing on the frame at
#: any depth — the one channel that must never be lost in the backdrop.
AID_BEACON = "#ffffff"

SHIP_HULL = "#dfe8ff"
SHIP_GLASS = "#5ff0ff"
THRUST = "#7ce8ff"
SHOT = "#9ffcff"

HUD_TEXT = "#e8eeff"
HUD_DIM = "#6b7699"
LIFE_ICON = "#5ff0ff"
STRIKE = "#ff2fa8"
WARN = "#ff2fa8"

TITLE = "#ffc247"
SUBTITLE = "#ff2fa8"
MENU_BOX = "#0d1230"
MENU_SELECTED = "#ffc247"
MENU_SELECTION_BG = "#1c1a3e"

# ── Layout ──────────────────────────────────────────────────────────

#: The score bar above the frame.
HEADER_ROWS = 1
#: The contact strip and the key hints below it. Two rows, and the strip is not
#: decoration: it is the game's second identification channel, drawn so that a
#: cluttered frame never hides the answer. It must never be drawn *over* the
#: frame it explains, which is why it has rows of its own.
FOOTER_ROWS = 2

#: Smallest terminal the game draws in.
#:
#: The frame is letterboxed to fit, so the floor is set by legibility rather
#: than by clipping: at 60 columns the whole canvas needs 17 playfield rows,
#: and the header and footer want three more.
MIN_COLS = 60
MIN_ROWS = HEADER_ROWS + 17 + FOOTER_ROWS

MENU_MIN_COLS = 44
MENU_MIN_ROWS = 16

# Menu geometry in cells, passed to the engine's Menu widget in place of its
# pixel defaults, which would sit entirely off-screen.
MENU_W = 34
MENU_ITEM_H = 1
MENU_TITLE_H = 0
MENU_PAD = 1
MENU_BORDER = 1

BANNER = "THE JOVIAN HUMANITARIAN CONFLICT"
TAGLINE = "know what you are shooting at"

#: How the name is set, best first: the block text, and whatever is left of it
#: in plain text beneath. Every rung spells the whole name.
TITLE_LADDER = (
    ("JOVIAN\nCONFLICT", "THE HUMANITARIAN"),
    ("JOVIAN", "THE HUMANITARIAN CONFLICT"),
)

# ── Glyphs ──────────────────────────────────────────────────────────
#
# Depth is carried by convergence in x, not by the glyph — see jovian.cells for
# why. What the glyph carries is *kind*, which is the question the game asks,
# and it says so in shape rather than in colour so that the answer survives a
# monochrome terminal.
#
# The far/near pair is the web build's third channel: silhouettes resolve from
# Z_SHAPE_READABLE inward. Far away both kinds are a single mark; close up a
# convoy is blunt and slab-sided and a hostile is an angular delta, which is
# exactly what these two pairs say with one character each.

#: A convoy, beyond and within silhouette range.
AID_FAR = "="
AID_NEAR = "▄"
#: A hostile, beyond and within silhouette range.
HOSTILE_FAR = "^"
HOSTILE_NEAR = "▲"

#: The transponder, drawn beside the hull at a constant size at every depth.
#:
#: Plain ASCII, deliberately. This is the most important character in the game
#: — the channel the fairness guarantee is written against — and the engine's
#: glyph table had no fallback for the heavy cross this used first, so on a
#: terminal that could not encode it the core mechanic would have arrived as
#: mojibake. A mark that needs no fallback cannot lose one.
BEACON = "+"

#: The ship. ASCII for the same reason: it is drawn every frame and it is how
#: you find yourself on the rail.
SHIP = "<A>"
SHOT_GLYPH = "|"
DECK_GLYPH = "~"
STAR_GLYPH = "·"
PARTICLE = "·"

#: Lives and strikes in the header. The heart is in the engine's fallback
#: table; the cross this used first was not, so it is a plain X.
LIFE_GLYPH = "♥"
STRIKE_GLYPH = "X"


def contact_glyph(kind: str, z: float, shape_readable: float) -> str:
    """The mark for a contact at this depth.

    One cell either way: a stand-in of a different width would move everything
    drawn after it, and the glyph fallback in the engine holds every substitute
    to one cell for the same reason.
    """
    near = z < shape_readable
    if kind == "aid":
        return AID_NEAR if near else AID_FAR
    return HOSTILE_NEAR if near else HOSTILE_FAR


def contact_colour(kind: str, *, dim: bool = False) -> str:
    """Colour is the fourth channel and never the only one — the glyph above
    has already said which kind this is."""
    if kind == "aid":
        return AID_DARK if dim else AID
    return HOSTILE_DARK if dim else HOSTILE


__all__ = [
    "AID", "AID_BEACON", "AID_DARK", "AID_FAR", "AID_NEAR", "AID_PALE",
    "BANDS", "BANNER", "BEACON", "DECK_FAR", "DECK_GLYPH", "DECK_LINE",
    "DECK_NEAR", "FOOTER_ROWS", "HEADER_ROWS", "HOSTILE", "HOSTILE_DARK",
    "HOSTILE_FAR", "HOSTILE_NEAR", "HUD_DIM", "HUD_TEXT", "LIFE_GLYPH",
    "LIFE_ICON", "MENU_BORDER", "MENU_BOX", "MENU_ITEM_H", "MENU_MIN_COLS",
    "MENU_MIN_ROWS", "MENU_PAD", "MENU_SELECTED", "MENU_SELECTION_BG",
    "MENU_TITLE_H", "MENU_W", "MIN_COLS", "MIN_ROWS", "PARTICLE", "SHIP",
    "SHOT", "SHOT_GLYPH", "SHIP_GLASS", "SHIP_HULL", "STAR", "STAR_GLYPH",
    "STRIKE", "STRIKE_GLYPH", "SUBTITLE", "TAGLINE", "THRUST", "TITLE",
    "TITLE_LADDER", "VOID", "VOID_HAZE", "WARN", "contact_colour",
    "contact_glyph",
]
