# The Jovian Humanitarian Conflict

An on-rails shooter over the cloud decks of Jupiter. Aid convoys fly among the
hostiles and squawk a transponder you must learn to read — shoot one and it
costs you the run.

> know what you are shooting at

## Versions

| | |
|---|---|
| `web/` | Browser version. Vanilla JS over the Canvas API. Play it at [magmacrunch.com](https://magmacrunch.com/arcade/jovian-humanitarian-conflict/) |
| `tui/` | Terminal version, on the `magmacrunch.engine` TUI engine. `python -m jovian`, and a cabinet in the arcade. Publishes as `magmacrunch-jhc` |
| `wii/` | Wii version, on [magnolia](../../engines/magnolia). Rules, renderer and audio all ported, runs in Dolphin at 60fps; not yet on real hardware. See `wii/README.md` |

`web/` is the source of truth for rules and tuning. The website repo copies it
into `arcade/jovian-humanitarian-conflict/` for deployment; its copy is
generated and should never be edited directly.

## Playing

Arrows or WASD to fly, `Z` or `Space` to fire, `P` to pause, `M` for music.

Aid convoys squawk a **double blink** and are worth 500 escorted clear.
Hostiles are dark and worth 100 × combo. A convoy lost to hostiles costs the
combo; shooting one yourself costs 1,000 points and one of three strikes. Three
strikes ends the run whatever the score says.

The transponder is readable from the frame a contact spawns, and a convoy
always completes two full squawk cycles before it can possibly be shot. That is
an arithmetic guarantee, not a courtesy — see `AGENTS.md`.

## Running it locally

`web/` needs to be served rather than opened as a file, because the modules are
plain scripts and the music is fetched:

```
cd web && python -m http.server
```

Music will not play out of a bare checkout: the track lives on the website's
jukebox and the game points at it there rather than shipping a second 2.7 MB
copy. Sound effects are synthesised and work anywhere.

## Tests

```
node web/tests/test-simulation.js
```

62 checks, run against the real shipped modules in a `vm` context rather than
against a reimplementation.

## Licence

PolyForm Noncommercial 1.0.0 — see `LICENSE`, and `NOTICE` for what is reserved
outright.
