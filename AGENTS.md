# The Jovian Humanitarian Conflict — agent brief

One game, one repo, versions beside each other:

- `web/` — browser version (vanilla JS over the Canvas API, plus adenosine's
  `AdAudio` for music). **Source of truth for rules and tuning**, in
  `js/config.js`. Deployed by the website repo, which copies `web/` into
  `arcade/jovian-humanitarian-conflict/`. Never edit the website's copy
  directly — it gets overwritten.
- `tui/` — terminal version, on the `magmacrunch.engine` TUI engine.
  `python -m jovian`, and a cabinet in the arcade. Publishes as
  `magmacrunch-jhc`. Its rules are a transcription of `web/js/` and are checked
  against it by running the real JavaScript in a node vm — see
  `tui/tools/js_oracle.mjs`. A tuning change to `js/config.js` therefore fails
  the Python suite until it is carried across, which is the point.

There is no `wii/` and none is planned, which makes this the first game here
whose README cannot open "the third sibling to web/ and wii/".

A rules change is not done until every version that exists has it, or the
commit says why one is skipped.

## AI Attribution

**No AI attribution.** Do not append `Co-Authored-By: Claude …`, "Generated
with …", or any similar trailer to commit messages, PR bodies, or release
notes. If your tooling adds such a line by default, remove it before
committing.

## The premise is a fairness invariant, not a feeling

The whole game is that refusing to shoot is a *decision* rather than a gamble.
That is only true if a convoy squawks its transponder long enough to be
identified before it can possibly be shot, which is a relationship between four
constants:

```
(Z_FAR - Z_FIRE_MAX) / RAIL_SPEED_MAX  >=  BLINK_PERIOD * 2 + reaction
(1100  -        600) /            6.0  =   83.3  >=  78            ok
```

`Z_FAR` and `Z_FIRE_MAX` are sized from the blink budget rather than the other
way round. A plausible-looking tuning pass — raising `RAIL_SPEED_MAX` for a bit
more pace — leaves the game fun and quietly unfair. `tests/test-simulation.js`
asserts this on the constants *and* again by simulation. Run it after touching
any of them.

## Identification has four channels, and they are ranked

`js/entities.js` opens with the list. The order is deliberate and each one is a
fallback for the one above:

1. **Transponder blink** — drawn at a *constant screen size* at every depth, so
   it is readable from the spawn frame. Motion, not colour. This is the channel
   the fairness test is written against.
2. **HUD contact strip** — drawn in `main.js` from `listContacts()`, so a
   cluttered frame never hides the answer.
3. **Silhouette** — from `Z_SHAPE_READABLE` inward only.
4. **Colour** — last, and never alone. Amber against magenta, never red/green,
   and the two differ in luminance as well as hue so the distinction survives
   greyscale.

Do not let a change collapse these into one. The reason the terminal port is
even possible is that channel 1 owes nothing to resolution or hue.

## The cache-buster stamps name files that are not in this repo

Every `?v=` in `web/index.html` is the first eight hex of SHA-256 over the file
it stamps, newlines normalised to LF. Get one wrong and visitors keep serving
cached old bytes.

Seven of them point at `../shared/` — `arcade-base.css`, `adenosine-audio.js`,
`adenosine-score-client.js`, `score-server.js`, `chat-widget.css`,
`adenosine-chat.js`, `chat-server.js`. Those files **do not exist here**. They
resolve only once `web/` has been copied into the website's `arcade/`, and they
go stale when *that* repo updates the shared bundles — which nothing here can
notice.

The website's `.githooks/pre-commit` recomputes stale stamps in its copy, but
that repair never travels back, and the sync copies `web/` over the website's
folder verbatim. So a stamp corrected there is reverted by the next sync,
silently. **The fix belongs in this repo.** Same trap as
`texas-holdem-lava-dome`; see its AGENTS.md for the commands.

## The music lives on the jukebox, not here

`CONFIG.MUSIC.URL` is `../../music/jukebox/songs/…` — a *website* path. The
track is 2.7 MB and is on the jukebox in its own right, so a local copy would
be pure duplication, and the config says so. The consequence is that **`web/`
is not standalone from this repo**: opened directly out of a checkout the game
runs with no music. It resolves correctly once synced into the website, which
is the only place it is served from.

Roderick Tron makes the same trade. If either is ever made standalone, do both.

Sound *effects* are not affected: `js/sfx.js` synthesises all six procedurally
and deliberately does not use `AdAudio` for them, so the game ships exactly one
audio asset and it is the one that lives elsewhere.

## Tests

```
node web/tests/test-simulation.js
```

The real shipped modules are loaded into a `vm` context — nothing there is a
reimplementation that could drift. 62 checks covering the fairness invariant,
friendly-fire attribution, dt invariance and projection monotonicity.

Friendly-fire attribution is worth singling out: a convoy killed by a hostile
costs a combo, one killed by the player costs 1,000 points and a strike.
Swapping those is the cruellest bug this game could have, because it punishes
the player for the thing they did right.

Until this repo existed those tests ran in no CI anywhere. They do now, on
every push, in `.github/workflows/ci.yml`.

## Releasing

`tui/` publishes to PyPI as `magmacrunch-jhc` on a `tui-v*` tag. `web/` has no
release of its own — the website repo syncs it — which is why the tag carries
the prefix rather than being a bare `v0.1.0`.

**Two things must happen before the first tag, in this order.**

A *pending publisher* has to exist on pypi.org, because the project does not
exist there yet and Trusted Publishing has nothing to match against: Your
account → Publishing, owner `magmacrunch-media`, repository
`jovian-humanitarian-conflict`, workflow `release.yml`, environment blank.
Skip it and every step passes and the publish alone fails with
`invalid-publisher`. Nothing is published when that happens, so the version
stays free and re-running once the publisher exists is enough — but it is a
confusing ten minutes, and the whole org hit it once already when the rename
invalidated the other four publishers.

And this releases **before** `magmacrunch` 0.8.0. The arcade lists its cabinets
as dependencies, so an arcade published before this package exists is an
unresolvable install for everybody, not only for people who wanted this game.

The workflow runs the web build's own suite first, then the Python one, then
runs the two oracle suites again on their own and fails if they *skipped*.
They skip themselves where node is absent, which is right on a laptop and
wrong at a release: publishing a port whose agreement with the shipped
JavaScript went unchecked is the one failure this repository is arranged to
prevent.

## Licence

PolyForm Noncommercial 1.0.0, matching the other cabinets. The engines it
borrows from stay Apache-2.0 in their own repositories.
