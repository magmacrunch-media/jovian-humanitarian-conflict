"""The game on a terminal that can encode nothing.

The engine substitutes in the renderer, so this game asks the question of
nobody — but it can still *draw* a character the engine's table has never heard
of, and then the substitution has nothing to substitute. That is not
hypothetical: this game shipped its transponder as a heavy cross first, which
is exactly the glyph with no fallback, and the transponder is the channel the
whole fairness guarantee is written against.

So the sweep below is the guard that matters most here, and it is the reason
the beacon is a plain plus sign now.
"""

import ast
import asyncio
import pathlib
import unicodedata

import pytest

pytest.importorskip("textual", reason='needs: pip install -e ".[dev]"')

from magmacrunch.engine.core.tui_host import TuiHost  # noqa: E402
from magmacrunch.engine.ui.glyphs import FALLBACKS, Glyphs  # noqa: E402

from jovian.app import JovianApp  # noqa: E402
from jovian.arcade import GAME  # noqa: E402
from jovian.scenes import GameScene  # noqa: E402

PACKAGE = pathlib.Path(__file__).resolve().parent.parent / "jovian"


def _drawn_literals(path):
    """Every string literal that is not a docstring.

    Comments fall out for free — they are not literals — and docstrings are
    excluded because prose *about* a glyph is not a glyph being drawn.
    """
    tree = ast.parse(path.read_text(encoding="utf-8"))
    docs = set()
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef,
                             ast.AsyncFunctionDef)):
            doc = ast.get_docstring(node, clean=False)
            if doc:
                docs.add(doc)
    for node in ast.walk(tree):
        if (isinstance(node, ast.Constant) and isinstance(node.value, str)
                and node.value not in docs):
            yield node.value


def test_every_glyph_this_game_draws_has_a_fallback():
    """The engine's own sweep covers the launcher, not the cabinets.

    magmacrunch 0.7.1 added a static sweep over its own package after the
    arcade's card borders turned out to have no stand-ins. That sweep cannot
    see this repository, so the cabinet needs its own — and it earned it
    immediately: the transponder, the ship and the strike marker were all
    drawn with characters the table did not know.
    """
    missing: dict[str, set[str]] = {}
    for path in sorted(PACKAGE.glob("*.py")):
        for literal in _drawn_literals(path):
            for char in literal:
                if ord(char) > 126 and char not in FALLBACKS:
                    missing.setdefault(char, set()).add(path.name)

    assert not missing, "glyphs drawn with no fallback: " + ", ".join(
        f"U+{ord(c):04X} {unicodedata.name(c, '?')} in {sorted(w)}"
        for c, w in sorted(missing.items())
    )


def test_the_transponder_needs_no_fallback_at_all():
    """The one channel that must never fail. A mark that is already ASCII
    cannot be substituted wrongly, cannot be substituted at a different width,
    and cannot be missed out of a table."""
    from jovian import theme

    assert theme.BEACON.isascii()
    assert len(theme.BEACON) == 1


def _shot(glyphs, size=(80, 24), frames=90):
    async def go():
        from magmacrunch.engine.core.tui_game import _GameApp

        host = TuiHost(title=GAME.info.title, fps=GAME.info.fps,
                       hold_ms=GAME.info.hold_ms, glyphs=glyphs)
        app = JovianApp(host, seed=7)
        host.push_scene(app.root_scene)
        host.stack.update(0.0)
        host.push_scene(GameScene(app))
        host.stack.update(0.0)
        for _ in range(frames):
            host.stack.update(1 / 30)

        tapp = _GameApp(host.game, host.game.surface)
        host.game._app = tapp
        async with tapp.run_test(size=size):
            await asyncio.sleep(0.35)
            text = host.game.surface.buffer.to_text()
            tapp.exit()
        host.quit()
        return text

    return asyncio.run(go())


@pytest.mark.parametrize("encoding", ["ascii", "cp1252", "cp437"])
def test_a_run_draws_nothing_the_terminal_cannot_encode(encoding):
    text = _shot(Glyphs(encoding=encoding))
    leaked = sorted({c for c in text
                     if c.encode(encoding, "ignore").decode(encoding, "ignore") != c})
    assert not leaked, (
        f"on {encoding} the game drew: "
        + ", ".join(f"U+{ord(c):04X} {c!r}" for c in leaked)
    )
    assert "SCORE" in text, "nothing rendered, so this proves nothing"


def test_the_plain_frame_has_the_same_layout_as_the_fancy_one():
    """One cell per substitute, so a plain terminal sees the same frame rather
    than a reflowed approximation of it."""
    plain = _shot(Glyphs(encoding="ascii"))
    rich = _shot(None)
    assert [len(r) for r in plain.split("\n")] == [len(r) for r in rich.split("\n")]
