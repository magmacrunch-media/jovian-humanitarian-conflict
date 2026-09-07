/* =====================================================================
 * config.h -- The Jovian Humanitarian Conflict (Wii)
 * Every constant, the palette, and the difficulty curve.
 *
 * A direct port of web/js/config.js. The numbers are deliberately the same
 * ones: the web version is the source of truth for rules and tuning, and a
 * console build that quietly drifts to its own balance is two games wearing
 * one name. Where a value differs it is because the console forced it, and it
 * says so on the line.
 *
 * ---------------------------------------------------------------------
 * The per-frame convention, and the one thing main.c must get right.
 *
 * Every rate below is expressed in "units per 60fps frame" and multiplied by
 * dt at the point of use. Anything measured in frames -- a cooldown, a
 * telegraph window -- is likewise a count of 60fps frames, decremented by dt
 * rather than by 1. That is inherited from the browser and kept unchanged,
 * because it is what lets the two versions be compared number for number.
 *
 * So `dt` here is NOT seconds. magnolia's clock_dt() is, so main.c converts:
 *
 *     float dt = clock_dt() * 60.0f;
 *
 * A Wii frame is 1/60 on NTSC and 1/50 on PAL, which makes dt 1.0 and 1.2
 * respectively -- the same game at both, which is the entire reason for the
 * convention and is asserted in tests/test_simulation.c.
 * ===================================================================== */
#ifndef CONFIG_H
#define CONFIG_H

/* -- The design frame -------------------------------------------------
 * The web canvas is 480x270. The simulation keeps those coordinates and
 * render.c maps them onto the Wii's safe area, exactly as makemecookies keeps
 * its 960x420. Everything below -- the rail's half-width, the ship's box, the
 * spawn spread -- is stated against this frame, so the tuning transfers
 * without a single number being re-derived.
 */
#define CANVAS_W 480
#define CANVAS_H 270

/* -- The rail ---------------------------------------------------------
 *
 * World space is (x, y, z): x right, y down, z into the screen. z = 0 is the
 * plane the player's ship sits on; contacts spawn at Z_FAR and travel toward
 * the camera. Screen scale is FOCAL / (z + FOCAL), so it is exactly 1.0 at
 * z = 0 and falls off hyperbolically.
 *
 * FOCAL is the one number that decides whether this game is legible. It was
 * 220 to begin with, which put every contact in a sixteen-pixel band at the
 * horizon at three to eight pixels wide. 520 trades some of that rush for a
 * frame you can play in: a hostile is 7px at spawn and 10px by the time it
 * can be shot. See web/js/config.js for the full working.
 */
#define FOCAL      520.0f
#define Z_FAR     1100.0f   /* spawn depth */
#define Z_NEAR     (-60.0f) /* past the camera; contacts are retired here */
#define HORIZON_Y  118.0f   /* design-frame y the vanishing point sits at */

/* Rail speed ramps with difficulty and then holds. The cap is not a taste
 * decision: it is what keeps (Z_FAR - Z_FIRE_MAX) / RAIL_SPEED above the
 * telegraph budget below, and the test suite asserts the two agree.
 */
#define RAIL_SPEED_MIN 4.2f
#define RAIL_SPEED_MAX 6.0f

/* Rail units to full difficulty -- about 1m45s at ~5 units a frame. */
#define DIFFICULTY_DISTANCE 32000.0f

/* How far the vanishing point slides against the ship. Banking left swings
 * the world right, which is most of what sells the depth. */
#define PARALLAX 0.35f
#define CAM_LAG  0.08f      /* fraction of the gap the camera closes per frame */

/* -- Player -----------------------------------------------------------
 * The ship flies in the z = 0 plane, so its world x/y are screen offsets from
 * the vanishing point and need no projection.
 */
#define SHIP_X_RANGE 168.0f /* half-width of the box the ship may occupy */
#define SHIP_Y_MIN   (-70.0f)
#define SHIP_Y_MAX   84.0f
#define SHIP_Y_START 10.0f
#define SHIP_ACCEL   0.85f
#define SHIP_DRAG    0.86f  /* per-frame velocity multiplier */
#define SHIP_SPEED_MAX 5.4f
#define SHIP_W 26.0f
#define SHIP_H 14.0f
#define BANK_MAX 1.0f       /* |bank| at full lateral speed, drives the roll */

#define MAX_LIVES 3
#define INVINCIBLE_FRAMES 100.0f

/* -- Guns -------------------------------------------------------------
 * Hit boxes are a BOX per sprite, not one radius for everything, and the
 * margins deliberately favour the player in both directions: a hostile is
 * easier to hit than it looks, a convoy is exactly as big as it looks. The
 * first cut had this backwards and every near miss punished you twice.
 */
#define SHOT_COOLDOWN 9.0f
#define SHOT_SPEED    26.0f /* z units per frame, away from the camera */

#define HOSTILE_HIT_MARGIN_W 7.0f
#define HOSTILE_HIT_MARGIN_H 6.0f
#define AID_HIT_MARGIN_W     0.0f
#define AID_HIT_MARGIN_H     0.0f

/* Shots do nothing beyond this depth. It sits well inside the window where
 * every identification channel is already legible, so there is no such thing
 * as a shot you were not given the information to hold. */
#define Z_FIRE_MAX 600.0f

/* -- Contacts ---------------------------------------------------------- */
#define HOSTILE_W 22.0f
#define HOSTILE_H 16.0f
#define AID_W     34.0f
#define AID_H     20.0f

/* Half-height of the band contacts spawn into. Inside the ship's own Y box,
 * so everything that spawns can be reached. */
#define CONTACT_Y_SPREAD 52.0f

/* Lateral drift, in world units per frame, while closing. */
#define HOSTILE_DRIFT 0.9f
#define AID_DRIFT     0.32f

/* The transponder. Aid convoys squawk a steady double-blink; hostiles are
 * dark. BLINK_PERIOD is the full cycle in 60fps frames: 30 = 2Hz.
 *
 * On the console this channel is drawn at a constant size in DESIGN pixels,
 * which render.c then scales with everything else -- so it stays constant
 * with depth, which is the property the fairness argument rests on, while
 * still growing with the display. A three-pixel mark left literal at 640x480
 * would be a speck behind a TV's bezel. */
#define BLINK_PERIOD 30
#define BLINK_ON_1   6      /* frames 0..6 lit */
#define BLINK_ON_2   14     /* and 10..14 lit -- a double-tap, not a pulse */
#define BLINK_GAP    10

/* Fairness budget. A convoy must complete two full squawk cycles before it
 * can possibly be shot, plus a human reaction allowance:
 *
 *     (Z_FAR - Z_FIRE_MAX) / RAIL_SPEED_MAX  >=  60 + 18
 *     (1100  -        600) /            6.0  =  83.3  >=  78     ok
 *
 * Z_FAR and Z_FIRE_MAX are sized from this rather than the other way round.
 * Widen the gap or slow the rail if either end is retuned; the test asserts
 * this rather than trusting it. */
#define TELEGRAPH_MIN_FRAMES 60.0f
#define REACTION_FRAMES      18.0f

/* Depth at which a convoy's transponder is HEARD as well as seen. Well
 * outside Z_FIRE_MAX on purpose: the point of the sound is to say a convoy is
 * inbound while you still have every option, including the option to stop
 * shooting. */
#define Z_PING 900.0f

/* Silhouettes become readable around here; colour is the last channel and
 * never the only one. */
#define Z_SHAPE_READABLE 410.0f

/* -- Spawning ---------------------------------------------------------
 * *_EARLY / *_LATE pairs are interpolated by difficulty in jov_diff_*().
 */
#define SPAWN_INTERVAL_EARLY 84.0f
#define SPAWN_INTERVAL_LATE  32.0f

/* Two from the very first wave, not one: the difference between a convoy and
 * a hostile is the only thing this game asks you to learn, so the opening
 * shows them side by side rather than one a wave apart. */
#define WAVE_SIZE_EARLY 2.0f
#define WAVE_SIZE_LATE  3.0f

/* Share of spawned contacts that are aid convoys. Deliberately flat: the
 * moral pressure must not thin out as the shooting gets busier. */
#define AID_SHARE_EARLY 0.30f
#define AID_SHARE_LATE  0.26f

/* How readily hostiles break off to attack a convoy rather than the player. */
#define HOSTILE_AGGRO_EARLY 0.25f
#define HOSTILE_AGGRO_LATE  0.75f

/* A hostile only locks a convoy it is actually near in depth. Without this a
 * hostile at the far end of the rail could mark a convoy at the near end, and
 * the line drawn between them crossed the whole screen pointing at something
 * the player could not yet reach. */
#define LOCK_MAX_DZ 260.0f

/* A convoy under fire dies this many frames after a hostile locks it, which
 * is the window you have to kill the attacker. Bounded from both sides:
 * 114 frames is what a perfect player needs to intercept from the worst case,
 * and much past 150 the convoy reaches the camera before the clock runs out
 * and the threat stops existing. See web/js/config.js for the measurements. */
#define AID_KILL_FRAMES 150.0f

/* Minimum lateral separation between two contacts spawned in one wave, so a
 * convoy is never hidden behind a hostile at the moment you must identify it. */
#define SPAWN_MIN_SEPARATION 62.0f
#define SPAWN_X_RANGE        150.0f

/* The cloud deck's bands recycle over their own depth span rather than Z_FAR,
 * so they converge into the haze the way the rails do. */
#define DECK_Z_SPAN 3200.0f

/* -- Scoring ----------------------------------------------------------- */
#define SCORE_HOSTILE        100
#define SCORE_ESCORT         500
#define SCORE_FRIENDLY_FIRE  (-1000)
#define COMBO_MAX            6
#define COMBO_WINDOW         180.0f  /* frames before a streak lapses */

/* Three friendly-fire hits end the run outright. Restraint is not optional
 * scoring advice -- it is a losing condition, which is the whole point of the
 * title. */
#define MAX_STRIKES 3

/* -- Effects ----------------------------------------------------------- */
#define PARTICLE_COUNT 10
#define PARTICLE_LIFE  24.0f
#define PARTICLE_SPEED 2.6f

/* -- Capacities -------------------------------------------------------
 * The browser pushes onto growable arrays. Fixed caps here: the Wii has no
 * business calling malloc sixty times a second, and the ceilings are sized
 * from what the game actually produces rather than guessed.
 *
 * MAX_CONTACTS: the rail holds Z_FAR/RAIL_SPEED_MIN = 262 frames of contacts,
 * and at the late spawn interval of 32 frames that is 9 waves of at most 3 --
 * 27, doubled for headroom.
 *
 * A wave that would overflow is DROPPED whole rather than half-spawned: half
 * a wave breaks the separation guarantee, which is the one property the
 * spawner exists to provide. Particles and popups evict oldest-first, because
 * a lost sparkle is a lost sparkle and a lost rule is a different game.
 */
#define MAX_CONTACTS  54
#define MAX_SHOTS     24
#define MAX_PARTICLES 96
#define MAX_POPUPS    16
#define MAX_EVENTS    32
#define POPUP_TEXT_MAX 20

/* -- Audio -------------------------------------------------------------
 *
 * The music is one file, converted by tools/convert-audio.sh out of the
 * website's jukebox -- it is not in this repo, and the game builds and runs
 * silently without it. These two numbers must match what that script was run
 * with; it prints them.
 *
 * Nothing in this game is timed off the music. makemecookies derives its whole
 * shift clock from the track's length and has to check the two against each
 * other at startup; here the track is a backdrop, the run ends on lives or
 * strikes, and a re-encode at a different rate needs nothing but these lines.
 */
/* The bed, as a fraction of full scale. The browser uses 0.42
 * (CONFIG.MUSIC.VOLUME) and can afford it: WebAudio mixes into float.
 * ASND sums into 16 bits and clamps, and the bed competes with up to
 * four concurrent effects for the same rail. */
#define MUSIC_LEVEL    0.30f
#define MUSIC_RATE     24000
#define MUSIC_CHANNELS 1

/* The resident audio budget, against the console's 24MB:
 *
 *   music.pcm   6351 KB   135.4s at 24kHz mono, linked into the .dol
 *   effects      144 KB   eight clips, synthesised at startup by sfx.c
 *
 * The music is forty times the effects, which is the whole reason the effects
 * are synthesised rather than recorded. If this ever has to shrink, the track
 * is the only place worth looking -- and trimming it costs a seam somebody has
 * to judge by ear.
 */

/* -- Palette -- the Jovian cloud decks --------------------------------
 * Warm ammonia bands over a cold void. Aid amber against hostile magenta:
 * never red/green, and the two differ in luminance as well as hue so the
 * distinction survives being read in greyscale -- or on a TV whose colour
 * nobody has calibrated since 2008.
 *
 * 0xRRGGBBAA, the byte order GRRLIB wants, so the renderer never has to
 * shuffle channels.
 */
#define C_VOID         0x05060FFF
#define C_VOID_HAZE    0x0D1230FF
#define C_STAR         0xC8D4FFFF

#define C_BAND_CREAM   0xE8CFA0FF
#define C_BAND_TAN     0xC99A63FF
#define C_BAND_UMBER   0x8F5F3CFF
#define C_BAND_SHADOW  0x5C3A28FF
#define C_BAND_DEEP    0x38222AFF
#define C_SPOT_RED     0xB4523FFF
#define C_SPOT_CORE    0x8C3628FF

#define C_DECK_NEAR    0x7A5A7EFF
#define C_DECK_FAR     0x2A2340FF
#define C_DECK_LINE    0xB489C4FF

#define C_HOSTILE      0xFF2FA8FF
#define C_HOSTILE_DARK 0x7A1150FF
#define C_HOSTILE_EYE  0xFFD0ECFF

#define C_AID          0xFFC247FF
#define C_AID_PALE     0xFFF2CFFF
#define C_AID_DARK     0x8A5F12FF
#define C_AID_BEACON   0xFFFFFFFF

#define C_SHIP_HULL    0xDFE8FFFF
#define C_SHIP_STEEL   0x7F90B8FF
#define C_SHIP_SHADOW  0x2B3350FF
#define C_SHIP_GLASS   0x5FF0FFFF
#define C_THRUST       0x7CE8FFFF
#define C_THRUST_HOT   0xFFFFFFFF

#define C_SHOT         0x9FFCFFFF
#define C_SHOT_CORE    0xFFFFFFFF

#define C_HUD_TEXT     0xE8EEFFFF
#define C_HUD_DIM      0x6B7699FF
#define C_LIFE_ICON    0x5FF0FFFF
#define C_STRIKE       0xFF2FA8FF
#define C_WARN         0xFF2FA8FF
#define C_PARTICLE_HOT  0xFFD8A0FF
#define C_PARTICLE_COOL 0x7A6A92FF

#endif
