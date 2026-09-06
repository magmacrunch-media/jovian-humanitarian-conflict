# The Jovian Humanitarian Conflict — terminal version

The second version, alongside `web/` (browser). Runs on the
[magmacrunch.engine](https://pypi.org/project/magmacrunch/) terminal backend.

**Work in progress.** The rules are ported and tested; the screens are not
written yet, so there is nothing to play here today. What exists:

| | |
|---|---|
| `jovian/config.py` | Every constant, transcribed from `web/js/config.js` |
| `jovian/projection.py` | The pseudo-3D transform, pure |
| `tests/test_physics.py` | The rules, with no engine on the machine |
| `tests/test_oracle.py` | The port, checked against the shipped JavaScript |

## Why a terminal port is possible at all

The game's central mechanic is a transponder blink drawn at a **constant screen
size at every depth** — motion rather than colour, and readable from the frame
a contact spawns. That channel owes nothing to resolution, sub-cell position or
hue, so it survives a character grid intact. The web build's own
`js/entities.js` ranks its four identification channels and puts it first for
exactly that reason.

The palette makes the same point from the other side: amber against magenta,
never red/green, chosen so the distinction "survives being read in greyscale".
A game built to stay legible without hue is one that survives `NO_COLOR`.

## Running the tests

```
python -m pytest
```

`python -m pytest` rather than bare `pytest`, because the module form puts the
current directory on `sys.path` and the package is not installed. No engine is
needed: `config` and `projection` import nothing outside the standard library,
and a test asserts that rather than trusting it.

`tests/test_oracle.py` additionally needs `node`, which it uses to run the real
`web/js/` modules in a vm and compare every number against the Python. It skips
where node is absent, so a Python-only machine still runs the rest.

## Development

```
pip install -e ".[dev]"
python -m pytest
ruff check .
```

## What is still to come

The screens, the arcade entry point and the command, and the cell mapping that
turns the 480x270 canvas into character rows. The last of those is the open
question: `FOCAL` spreads contacts across 270 pixels of smooth falloff, and a
terminal has perhaps eighteen rows with nothing between them, so depth probably
needs a second channel — glyph weight, or a dim-to-bright ramp — rather than
row position alone.

## Licence

PolyForm Noncommercial 1.0.0. `LICENSE` and `NOTICE` here are copies of the
ones at the repository root, carried so they land inside the built wheel.
