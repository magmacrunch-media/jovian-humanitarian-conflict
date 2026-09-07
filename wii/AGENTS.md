# The Jovian Humanitarian Conflict — Wii port, agent brief

The console version, built on [magnolia](../../../engines/magnolia). `web/` is
still the source of truth for rules and tuning; this is a port of it, and the
job of `tests/test_simulation.c` is to keep that true.

Read `../AGENTS.md` first — the no-AI-attribution rule, the fairness invariant
and the four identification channels apply here unchanged, and this file does
not repeat them.

## What is finished and what is not

**Finished, and verified here:** the rules. `source/sim.c` is a port of
`web/js/player.js`, `world.js` and `entities.js` plus the scoring policy out of
`main.js`, and `tests/test_simulation.c` is the web suite's cases ported case
for case — 82 checks, run by `make test` with nothing but a C compiler. The
port was checked by mutation as well as by running; see below.

**Finished:** `source/render.c`. No longer a placeholder — it is a port of the
drawing halves of `web/js/world.js`, `entities.js` and `player.js`, with the
same shapes, the same gradients and the same palette.

Worth being clear about what "port" means here, because it is not the usual
thing: the browser has **no sprite assets**. It draws everything procedurally
through the Canvas API, so matching it means matching its *drawing calls*, not
baking its output into PNGs. `sprites/` is empty because there is nothing to
put in it, not because nobody has got round to it.

Three deviations from the browser, all deliberate:

- **The gas giant's terminator is folded into the band colours** rather than
  laid over the finished disc as a translucent gradient. GX has no circular
  clip, so the disc is drawn as horizontal strips chorded to the circle; once
  you are doing that, shading each strip as it goes is one pass instead of
  three and does not blend against whatever is behind the planet at the limb.
- **The beacon's halo is a translucent square, not a radial gradient.** The
  browser has `Renderer.glow`; this has eight pixels of 30% white behind four
  of solid. At the size the mark is drawn, the difference is invisible.
- **Popups are drawn at a fixed size 12** rather than the browser's 6px scaled
  by depth. A 6px glyph on a TV is not a glyph.

Two earlier bugs in this file were found by *looking at a frame* rather than by
any test, which is the argument for capturing one whenever it changes. The gas
giant was drawn as one rectangle per band sized to that band's widest point — a
stepped layer cake, widest below the equator and never closing at the bottom,
which is not a sphere and looked like one. And the beacon drew its bright core
first and its translucent halo over the top, washing out the one mark that must
not be weak. Neither was anything a host test could have caught, and the second
would have degraded the fairness channel while looking fine in the source.

**Absent:** all audio, the scoreboard, an attract mode. magnolia brings scoring
up in `magnolia_init()` and none of it is wired. (Not sprites — see above; the
browser has none either.)

**Run in Dolphin, 2026-09-06, and not yet on real hardware.** Under
`AUTOPILOT=1` it boots, plays a full run and shuts itself down: 17.8 seconds,
score 4800, 3 kills, 8 convoys escorted, none lost, no strikes, rank EXEMPLARY,
ended by running out of ships. `events_dropped=0` and `waves_dropped=0` on the
console as well as on the host. Three separate builds produced a byte-identical
run — `AUTOPILOT` seeds from `clock_frame()` before the loop starts, which is
always 0, so an autopilot run is deterministic by construction and any
difference between two of them is a real difference.

A Wii is not Dolphin, so the hardware claim is still unmade.

## This game ships no audio, and that is a decision the web version made

Every other magnolia game here converts a track. There is nothing to convert:

- The **music** is on the website's jukebox, not in this repo.
  `CONFIG.MUSIC.URL` in `web/js/config.js` is `../../music/jukebox/songs/…`, a
  *website* path, because the track is 2.7 MB and lives there in its own right.
  So `web/` is not standalone from this repo either — opened out of a checkout
  the game runs silent. Roderick Tron makes the same trade.
- The **sound effects** are not files at all. `web/js/sfx.js` synthesises all
  six procedurally in WebAudio, deliberately not through adenosine's `AdAudio`.

So the web game ships exactly one audio asset and it is the one that lives
somewhere else. Wiring sound here means either fetching the ogg out of the
website repo and converting it the way makemecookies does, or reimplementing
six synthesised effects against `ASND` — both real work, neither started.

`jov_run_resolve()` already raises a `JovCue` per frame with a count for each
of the eight things that make a noise, and `main.c` reads it for shake and
flash. Wiring audio is a change inside that one block and nowhere else. That is
why the cues are counts rather than flags: two kills in a frame must not
silently become one.

## Text was the largest problem in this port, and it was fixed in the engine

**Resolved 2026-09-06 in magnolia, not here.** Kept because the measurements
are the useful part, and because the shape of the mistake recurs.

`GRRLIB_PrintfTTF` asked FreeType for every glyph on every call, and that was
the whole cost of drawing text. What it did to this game:

| | before | after |
|---|---|---|
| the results card, 360 frames | 30.0 s (12 fps) | 6.005 s (60 fps) |
| gameplay frames hitting the `dt` cap | 3 in an 18 s run | 0 |

The second row is the one that mattered. `dt` is capped at 2.0, so a frame that
took longer than two frames' worth **discarded time** rather than merely
stuttering — and every capped frame in the trace landed on a frame that drew a
score popup, which is to say the game slowed down in proportion to how much was
happening. That is a rules-adjacent fault wearing a renderer's clothes, and
`make test` passed throughout, because the rules are dt-correct and it was the
dt that was wrong.

magnolia now caches rasterised glyphs and their metrics; see its CHANGELOG.
Nothing in this game changed for it beyond a Makefile line adding FreeType's
headers to the include path.

**The diagnosis is the part worth keeping.** The obvious guess was per-pixel
plotting into the framebuffer, and batching would have been the obvious fix and
would have bought nothing. What settled it was two contrasts: the same glyph
count at four times the pixel area cost 10% more, and `GRRLIB_WidthTTF` — which
measures and rasterises nothing at all — cost almost as much as drawing. Cost
per glyph, not per pixel. Both cases are in `magnolia/bench/`, and the reason
they are kept is that a benchmark reporting only totals would have said "text is
slow", which everybody already knew.

Text is now cheap enough to stop thinking about: a whole results card is 380us
against a 16667us frame. It is not free, so do not put a thousand glyphs on
screen, but it no longer shapes what this renderer can do.

## The playfield is scissored; the HUD is not

A canvas clips at its own edges, so the browser can draw a deck band 1800 world
units wide and see only the part that lands on it. GX does not clip, and the
first version of this renderer let those bands run out across the letterbox and
into the overscan — long horizontal lines either side of the picture, which read
as a rendering fault rather than as a deck.

`rd_playfield_begin()` / `rd_playfield_end()` set a GX scissor around the
letterbox, and `main.c` wraps the rail, the contacts and the ship in them. The
pair is explicit rather than hidden inside `rd_draw_rail()` for one reason: the
title card draws the rail and then draws lettering over it, and the lettering
belongs to the HUD's full-safe-area space. A clip left on would cut the title in
half — which is why `rd_draw_title_text()` is separate from the backdrop rather
than one call that does both.

## The transponder is the one thing in render.c you may not simplify

`source/config.h` states the blink in the design frame and `render.c` draws it
at a **constant design size at every depth**, which is what makes a convoy
identifiable on the frame it spawns. Everything else in that file is a
placeholder waiting to be replaced; this is load-bearing.

It is also the channel that survives the console best. The web version's four
channels are ranked so that each is a fallback for the one above, and on a TV
across a room the lower three all degrade: silhouette loses to overscan and
composite blur, colour loses to a set nobody has calibrated since 2008, and the
HUD strip is at the bottom edge where a CRT eats it. Motion does not degrade.
If a change ever has to cost one of the four channels here, it costs the last
one, never the first.

## The blink runs on dt here, and on raw frames in the browser

`main.js` increments `frame` once per `requestAnimationFrame` and
`beaconLit(c, frame)` reads that counter, so on a 144Hz display the browser's
squawk is 2.4× faster than the 2Hz the constant names. Nobody notices, because
nothing else is keyed to it.

`jov_beacon_lit()` takes `sim->frame`, which accumulates **dt** rather than
counting frames, so the squawk is 2Hz in wall-clock on NTSC and on PAL alike.
That is a deliberate deviation and it is the safer direction: the fairness
budget is stated in 60fps frames, the rail advances in 60fps frames, and a
blink that ran on raw frames would be the one quantity in the game that did
not — 50Hz PAL would fit fewer squawk cycles into the same identification
window that `TELEGRAPH_MIN_FRAMES` says it must fit two into.

If the browser is ever fixed to match, delete this section rather than the
behaviour.

## Scoring lives in sim.c, which is not where the browser puts it

`main.js` owns the price list and `entities.js` knows nothing about it. That
split is right in the browser because `main.js` is reachable from the test vm.
`main.c` is **not** reachable from `make test` — it includes magnolia — so
pricing an event there would put friendly-fire attribution outside the suite,
and friendly-fire attribution is the second most costly thing in this game to
get wrong.

So `jov_run_resolve()` prices events and emits cues, and `main.c` turns cues
into shake, flash and (eventually) sound. No policy in `main.c`, no audio in
`sim.c`. The escort half of that price list is worth singling out: it is paid
when a convoy leaves the frame alive, which happens when the player did
*nothing*, so no bot exercises it. "Escort pays nothing" survived the first
version of this suite.

## The rules must stay engine-free

`source/sim.c` includes `<math.h>` and `<string.h>`; `source/projection.c`
includes `<math.h>`. No `grrlib.h`, no `ogc/`, no `magnolia.h`. That is what
lets `make test` link them on any machine with a compiler, and what would let
CI run them on a GitHub-hosted runner with no devkitPPC anywhere.

Keep it. A `GRRLIB_Rectangle` in `sim.c` costs the entire suite, and the suite
is the only thing standing between this port and the browser version quietly
becoming two different games. `TESTDEPS` in the Makefile is the list of files
that promise this.

The corollary: the input mapping lives in `main.c`. The simulation is handed an
axis in `[-1, 1]` and never sees a button, which is also why swapping the D-pad
for the Nunchuk's analog stick later is an engine change and not a change to
any rule.

## Fixed arrays, and the two counters that watch them

The browser pushes onto arrays that grow. Every array here is a fixed cap, so
two things can be silently lost, and `sim.h` keeps a counter for each:

- `waves_dropped` — a wave that will not fit is refused **whole**, never
  half-spawned, because half a wave breaks the separation guarantee that is the
  only reason the spawner exists.
- `events_dropped` — an event raised with the queue full. This is the nastier
  one: the contact still dies, the explosion still plays, and the number at the
  top of the screen is quietly light.

Both are asserted to stay at zero across 6000 frames of the worst case (firing
every frame, which maximises kills, explosions and events at once). Measured
peak contacts in that run is well inside `MAX_CONTACTS`. If either counter ever
moves, raise the cap — do not raise the tolerance.

## Mutation testing: 14 of 14

Running green proves a suite runs. It does not prove it would notice. Fourteen
rules were broken in turn and every one was caught:

friendly-fire attribution swapped · `RAIL_SPEED_MAX` raised past the telegraph
budget · hit-box margins swapped · the nearest-candidate rule dropped · drag
scaled by dt instead of raised to it · the camera chase likewise · the lock not
released when the attacker dies · the opening wave left to the dice · the
separation gap removed · the ping fired every frame · hostiles made to squawk ·
`MAX_CONTACTS` undersized · escort not paid · i-frames removed.

**Three of those survived the first pass, and the fixes are in the suite now:**
hostiles squawking (the ping check covers the *sound*, which is a different code
path from the *light*), escort not paid, and the camera chase — which slipped
through because the web suite's tolerance of 0.6 is looser than the correct
implementation needs. A correct `1 - (1 - k)^dt` compounds exactly, so the
honest tolerance is rounding error; at 0.6 the obvious wrong version lands 0.43
out and passes. That one is now checked at 0.02, and at two step sizes.

If you add a rule here, break it on purpose before you believe the suite.

## There is no oracle, and what one would cost

`tui/` checks itself against the shipped JavaScript by running it in a node vm
(`tui/tools/js_oracle.mjs`), so a tuning change to `js/config.js` fails the
Python suite until it is carried across. Nothing like that exists here, and it
is not an oversight:

- The RNG **is** shared. `jov_rng_next()` is mulberry32 in 32-bit integer
  arithmetic, bit-identical to the web suite's generator for a given seed, and
  it consumes randoms in the same order — including the short-circuit in
  `spawnWave` that means a convoy must *not* draw an aggro roll.
- The simulation is not. This runs in `float` where the browser runs in
  `double`, and libm's `sinf`/`cosf`/`powf` are not V8's `Math.sin`/`cos`/`pow`.
  Identical seeds give identical **spawns**; they do not give identical
  trajectories, and asserting they do would produce a suite that fails on a
  different libm.

An oracle is still buildable and would be worth it: compare the *spawn stream*
— kinds, lateral positions, the aggro flags — which is pure arithmetic over a
shared generator and should agree exactly. Trajectories would need a tolerance.
Nobody has written it.

What exists instead is a distribution check, run by hand on 2026-09-06. Twenty
seeds flown passively — never firing, never moving — through the real browser
modules in a vm and through `sim.c` on the host, with `main.js`'s price list
transcribed for the browser side so the two are scored alike:

|  | browser | this port |
|---|---|---|
| run length, median | 52.7 s | 50.6 s |
| run length, range | 20.3 – 87.9 s | 12.4 – 102.5 s |
| score, median | 14,000 | 13,000 |
| convoys escorted, median | 28 | 26 |

Close enough that the difference is two RNG streams rather than two games — and
worth re-running rather than trusting after any change to the rules. Note the
first attempt at this used three seeds and appeared to show the port dying
twice as fast; it was noise, and three samples is not a comparison.

**A passive player scores five figures and ranks well in both.** That is the
web version's balance, not something the port introduced, and it is the thing
to look at first if the tuning is ever revisited.

## Dolphin cannot be driven by a script — use `AUTOPILOT`

**Do not spend an afternoon on `SendInput`.** Dolphin's emulated Wiimote reads
the keyboard through DirectInput, and DirectInput does not observe injected
keystrokes. Neither `SendInput` nor `keybd_event` reaches the game, from a
foreground or a background process, and **nothing reports an error** — the keys
simply do nothing. The mouse is the exception, which is what makes it so
confusing: `Buttons/A` defaults to `Click 0`, so a script can start a run and
then find that no direction responds, reading exactly like a broken input
mapping. Measured on makemecookies; see its `wii/AGENTS.md` for the numbers.

So gameplay is verified with the compile-time hook instead:

```bash
make CFLAGS='-g -O2 -Wall $(MACHDEP) $(INCLUDE) -DAUTOPILOT=1'
```

That skips the title, flies a run unattended, shows the results for six seconds
and then stops driving. Both timeouts matter: without them the results card
waits for an A press that can never arrive, and `main()` starts a second run
immediately — which is how a capture ends up showing the *next* run's empty
scoreboard and getting filed as this one's result. **Returning from `main()`
does not close Dolphin**, so a scripted run still has to close the emulator
itself.

### Reading an unattended run: the log, not the screenshot

Under `AUTOPILOT` the game prints a per-second heartbeat and a run summary
through `printf`. **That is what an unattended run is read from.** A screenshot
can be of the wrong thing in three separate ways — the wrong run, the wrong
window, or a torn grab — and none of them announces itself. A trace line cannot
be any of those.

Capturing a frame is still worth doing, because two real bugs here were found by
looking and could not have been found any other way. Two things about it:

- **`CopyFromScreen` against the hardware backend comes back torn.** Dolphin
  presents through the GPU and the grab catches it mid-present: the first
  attempt returned the top fifth of the frame and pure white for the rest, which
  looks like the game having rendered a white screen. The tell is that the white
  is *pure* `#FFFFFF`, and almost nothing in this game is.
- **The software renderer captures cleanly.** `-v "Software Renderer"` on the
  command line blits into an ordinary window. The frames in this repo's history
  were taken that way. The *run* under it is not representative — it is far
  slower than 60fps — so use it to look at a frame, never to time one.

And pin the window with `SetWindowPos(HWND_TOPMOST)` rather than
`SetForegroundWindow`, which is refused to a background process while another
application holds focus, silently.

**Both builds write `build/jovian.dol`.** A plain `make` and a `make
CFLAGS='... -DAUTOPILOT=1'` produce the same path, so whichever ran last is what
is sitting there -- and an autopilot binary staged as the normal one plays a run
on its own and then exits, which in Dolphin looks like a magenta screen and a
"stop the current emulation?" prompt rather than like the wrong binary. `make
clean` between them, and check the hash if a capture surprises you.

`AUTOPILOT_EVERY` sets the reaction time in frames. Verified 2026-09-06:
`AUTOPILOT` defaults to 0 and compiles away entirely — the default build and an
explicit `-DAUTOPILOT=0` are byte-identical (`b18a7843…`), the build is
reproducible across two clean runs, and `-DAUTOPILOT=1` is a different binary,
as it must be.

**A clean autopilot run does not mean the game is tuned.** The bot reads the
transponder off the struct, so it never mistakes a convoy for a hostile and can
tell you nothing about whether a person could — which is the entire question
this game asks. Flown on the host, the sharp bot (6-frame reaction) scores
12,000–31,000 over 24–50 seconds and never loses a convoy; a slower one
(18 frames) scores a third of that. Only hands can say whether the transponder
is readable, and `../AGENTS.md` says the same about the web version's
bot-measured tuning.

## printf works, but only since magnolia 0.3.0 — and needs Logger.ini

`printf` reaches Dolphin's log through `SYS_STDIO_Report(true)`, which
`magnolia_init()` calls. Set `OSREPORT = True` and `WriteToFile = True` in
Dolphin's `Logger.ini`; both default to False, which makes a working trace look
like a dead one. Before 0.3.0 the engine did not make that call and every
`printf` in every game on it was discarded — a 0-byte `dolphin.log` even with
the Logger.ini half correct.

The lesson that outlives the bug: **a diagnostic nobody receives is worse than
none**, because it reads in the source as though the case is handled. Use
`printf` for tracing and the screen for anything a person has to act on.

## Tuning belongs to `web/`, not here

`source/config.h` carries the same numbers as `web/js/config.js` deliberately,
and the reasoning behind each is copied across with them rather than left
behind. If a Wiimote turns out to need different numbers — `SHIP_ACCEL` and
`SHIP_SPEED_MAX` are the two a D-pad is most likely to argue with, since the
browser's arrow keys are the same digital input but a TV is further away —
change them **in both files** and say in the commit that they were changed in
both. Two versions with two balances is two games.

`RAIL_SPEED_MAX` is the one that is not a taste decision. It is bounded by the
telegraph budget, the suite asserts the relationship, and raising it leaves the
game fun and quietly unfair.

## Building

Host tests need nothing but a compiler:

```bash
make test
```

The console build needs devkitPPC. It **is** installed in WSL on this machine
at `/opt/devkitpro` (devkitPPC 16.1.0, with GRRLIB in `portlibs/wii`).

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH=$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH

make            # build/jovian.dol
make dolphin    # stage and push to Dolphin's SD folder
```

Call WSL from PowerShell, not Git Bash — MSYS rewrites `/mnt/c/...` arguments
before WSL sees them and the call hangs rather than failing.

### Two Makefile traps, both already handled here

**The engine wildcard is evaluated twice.** `MAGNOLIA ?= $(firstword $(wildcard
../../magnolia ../../../engines/magnolia))` is written against this directory,
but `make` re-invokes itself with `-C build`, and in that second pass the
working directory is one level deeper, so both candidates miss and the guard
fires with the engine sitting exactly where it says it looked. This Makefile
passes the resolved value down:

```make
@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile MAGNOLIA=$(MAGNOLIA)
```

**An absolute `MAGNOLIA=` is not a workaround.** It gets past the guard and then
fails at `magnolia.h: No such file or directory`, because the devkitPro rules
build include paths as `$(TOPDIR)/$(dir)`, which only composes with a relative
one. An override has to be relative to this directory.

**`assets.s` is generated even with no assets.** There are none yet, and
invoking `bin2s` with no input files errors out, so the rule writes an empty
blob instead. Delete that branch the moment the first sprite or `.pcm` lands.

## Licence

PolyForm Noncommercial 1.0.0 — see `../LICENSE`, and `../NOTICE` for what is
reserved outright. magnolia stays Apache-2.0 in its own repository.
