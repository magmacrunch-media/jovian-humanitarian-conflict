"""What the package says about itself.

Its own file, not a section of ``test_app.py``, because that suite opens with
``importorskip("textual")`` and this check needs nothing installed — the whole
point of a hand-kept literal is that it is readable from a bare checkout. CI's
no-engine job runs it, so it gates every push.

The other two cabinets grew this file the same way and for the same reason:
their ``__version__`` sat at 0.1.0 through four releases, because the release
workflow compares the git tag against pyproject and never looks at the module.
This one starts with the check rather than earning it.
"""

import pathlib
import sys

import pytest


def _pyproject() -> dict:
    """The packaging metadata, read from source.

    ``tomllib`` is 3.11; this package supports 3.10. Skipping there rather than
    taking a ``tomli`` dependency for one test is the cheaper trade — CI runs
    3.12, so the check still gates every push.
    """
    if sys.version_info < (3, 11):
        pytest.skip("tomllib is 3.11+")
    import tomllib

    root = pathlib.Path(__file__).resolve().parent.parent
    return tomllib.loads((root / "pyproject.toml").read_text(encoding="utf-8"))


def test_the_version_is_the_one_the_package_declares():
    import jovian

    assert jovian.__version__ == _pyproject()["project"]["version"]


def test_the_engine_pin_is_high_enough_for_the_glyphs_it_uses():
    """0.7.1, not 0.7.0.

    0.7.0 shipped the glyph fallback without the box-drawing set the launcher's
    own chrome needed. Pinned a release too low the install succeeds and the
    frame is mojibake on exactly the terminal the fallback exists for, which is
    the worst place to learn about a pin.
    """
    deps = _pyproject()["project"]["dependencies"]
    assert any(d.replace(" ", "") == "magmacrunch>=0.7.1" for d in deps), deps


def test_the_entry_point_names_a_module_that_exists():
    """An entry point is metadata nothing validates at install time, so a typo
    installs cleanly and only surfaces as a cabinet the arcade cannot load."""
    group = _pyproject()["project"]["entry-points"]["magmacrunch.games"]
    assert group == {"jovian": "jovian.arcade:GAME"}

    from jovian.arcade import GAME

    assert GAME.info.key
