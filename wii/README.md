# The Jovian Humanitarian Conflict — Wii

The console version, built on
[magnolia](https://github.com/magmacrunch-media/magnolia).

> know what you are shooting at

`../web/` is the source of truth for rules and tuning. This is a port of it,
and `tests/test_simulation.c` is what keeps the two from drifting apart.

## State of it

The **rules are ported and tested**. `source/sim.c` is a port of the browser's
`player.js`, `world.js` and `entities.js`, and `make test` runs the web suite's
cases ported case for case — plus a few the console needs and a browser does
not.

What you can see is a **placeholder**. `source/render.c` draws the whole game
out of rectangles, lines and circles: the banded giant, the deck streaming past,
contacts and their transponders, the lock links and countdowns, the ship, the
HUD. Enough to play; not the look. One part of it is not a placeholder and is
marked as such — the transponder, which is the channel the whole premise rests
on.

There is **no sound at all**, which is unusual here and is inherited rather than
skipped: the web version's music lives on the website's jukebox and its six
sound effects are synthesised in WebAudio, so there was no asset to convert. See
`AGENTS.md`.

It has **not been on a console or in Dolphin yet.**

## Layout

```
wii/
├── Makefile
├── meta.xml          Homebrew Channel entry
├── source/
│   ├── config.h      the rail, the palette, and every tunable number
│   ├── projection.c  the pseudo-3D transform — pure maths, no engine
│   ├── sim.c         the ship, the rail, the contacts, the IFF rules, scoring
│   ├── render.c      draws state, decides nothing (placeholder)
│   └── main.c        the engine, the clock, the controller, the three screens
├── tests/            host tests, no console and no cross-compiler needed
├── sprites/          PNGs, embedded by bin2s (empty so far)
└── audio/            raw PCM, embedded by bin2s (empty, and see above)
```

## Playing

Hold the Wiimote **sideways**. The D-pad flies, `1` or `2` fires, `HOME` quits.

Aid convoys squawk a **double blink** and are worth 500 escorted clear.
Hostiles are dark and worth 100 × combo. A convoy lost to hostiles costs the
combo; shooting one yourself costs 1,000 points and one of three strikes. Three
strikes ends the run whatever the score says.

The transponder is readable from the frame a contact spawns, and a convoy
always completes two full squawk cycles before it can possibly be shot. That is
an arithmetic guarantee, not a courtesy — `../AGENTS.md` has the working, and
`make test` asserts it on the constants and again by simulation.

## Tests

```bash
make test
```

82 checks covering the fairness invariant, the audible transponder, friendly-
fire attribution, the price list, frame-rate independence, the projection, hit
boxes, the rescue window, the spawner's separation guarantee, flight, and the
fixed-array capacities. No console, no emulator, and no cross-compiler —
`source/sim.c` and `source/projection.c` are free of libogc and GRRLIB, which is
what makes that possible and why it has to stay that way.

Two of those are worth singling out.

**Friendly-fire attribution.** A convoy killed by a hostile costs a combo; one
killed by the player costs 1,000 points and a third of the run. Swapping them
would be the cruellest bug this game could have, because it punishes the player
for the thing they did right.

**Frame-rate independence**, which matters more here than in the browser: a Wii
frame is 1/60 on NTSC and 1/50 on PAL, and the same run has to play the same on
both. There is a check for exactly that pair.

The suite was also checked by breaking things on purpose — fourteen rules
mutated one at a time, all fourteen caught. Three of them were *not* caught by
the first version of the suite; see `AGENTS.md`.

## Building

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH=$DEVKITPPC/bin:$PATH

make            # build/jovian.dol
make deploy     # stage sdcard/apps/jovian/
make dolphin    # push that to the folder Dolphin reads as its SD card
```

`make dolphin` clears the app directory rather than merging, so **saved scores
and settings are deleted on every deploy**. That is right for a dev loop and
wrong to mistake for the game failing to save.

The engine is expected at `../../magnolia` or `../../../engines/magnolia`;
override with `make MAGNOLIA=<path>`, and the path has to be **relative** —
see `AGENTS.md` for why an absolute one fails later and more confusingly.

## Onto a real Wii

```bash
make card SD=/mnt/e             # install onto an SD card (permanent, merges)
make wii  WIILOAD=tcp:<wii-ip>  # send this build to a running console
```

`make card` needs the card's mount point because a removable drive's letter
moves — a card showing as `E:` in Windows is `/mnt/e` in WSL. Unlike
`make dolphin` it merges, so saves on the card survive an update.

`make wii` sends the `.dol` over the network and runs it immediately without
installing anything. The console has to be sitting on the Homebrew Channel's
netloader screen — open the channel and press Home — and it prints its own IP
address there.

## Licence

PolyForm Noncommercial 1.0.0 — see `../LICENSE`, and `../NOTICE` for what is
reserved outright.
