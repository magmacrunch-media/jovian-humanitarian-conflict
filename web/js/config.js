// config.js — The Jovian Humanitarian Conflict | MagmaCrunch Media © 2026
// Every constant, the palette, and the difficulty curve.
//
// Per-frame convention, inherited from Roderick Tron: every rate below is
// expressed in "units per 60fps frame" and multiplied by dt at the point of
// use, so the game runs identically on a 60Hz and a 144Hz display. Anything
// measured in frames (a cooldown, a telegraph window) is likewise a count of
// 60fps frames and is decremented by dt, not by 1.

const CONFIG = {
    CANVAS_W: 480,
    CANVAS_H: 270,

    // ── The rail ──────────────────────────────────────────────────────
    //
    // World space is (x, y, z): x right, y down, z into the screen. z = 0 is
    // the plane the player's ship sits on; contacts spawn at Z_FAR and travel
    // toward the camera. Screen scale is FOCAL / (z + FOCAL), so it is exactly
    // 1.0 at z = 0 and falls off hyperbolically — the same divide a real
    // perspective transform does, which is why distant things bunch up near
    // the horizon the way they should.
    // FOCAL is the one number that decides whether this game is legible.
    //
    // It was 220 to begin with, which is the dramatic choice: scale runs 1.0 to
    // 0.167 across the rail, so contacts rush at you six-fold. Measured against
    // the real projection, that put every contact in a SIXTEEN pixel band at
    // the horizon at three to eight pixels wide, sitting on the bright edge of
    // the cloud deck. The whole 480x270 frame was going unused and the sprites
    // were too small to read — fatal in a game whose only question is what you
    // are looking at.
    //
    // 520 trades some of that rush for a frame you can actually play in:
    //
    //     z=0    1.00        z=600 (firing range)  0.46
    //     z=410  0.56        z=1100 (spawn)        0.32
    //
    // Still a three-fold approach, but a hostile is 7px at spawn and 10px by
    // the time it can be shot, and contacts fan out across the width and height
    // of the frame instead of bunching on one line.
    FOCAL: 520,
    Z_FAR: 1100,         // spawn depth
    Z_NEAR: -60,         // past the camera; contacts are retired here
    HORIZON_Y: 118,      // screen y the vanishing point sits at

    // Rail speed ramps with difficulty and then holds. The cap is not a taste
    // decision: it is what keeps Z_FAR / RAIL_SPEED above the telegraph budget
    // below, and tests/test-simulation.js asserts the two agree.
    RAIL_SPEED_MIN: 4.2,
    RAIL_SPEED_MAX: 6.0,

    // Rail units to full difficulty. At ~5 units a frame this is about 1m45s,
    // near enough one play of the track. The first tuning ran this at 9000 and
    // the game was maxed out thirty seconds in, which left the whole middle of
    // a run with nowhere left to escalate to.
    DIFFICULTY_DISTANCE: 32000,

    // How far the vanishing point slides against the ship. Banking left swings
    // the world right, which is most of what sells the depth.
    PARALLAX: 0.35,
    CAM_LAG: 0.08,       // fraction of the gap the camera closes per frame

    // ── Player ────────────────────────────────────────────────────────
    //
    // The ship flies in the z = 0 plane, so its world x/y are screen offsets
    // from the vanishing point and need no projection.
    SHIP_X_RANGE: 168,   // half-width of the box the ship may occupy

    // Contacts fly in a band around y = 0 (see the spawn spread in
    // entities.js), so the ship's box is centred a little below that rather
    // than well under it. The first tuning rested the ship at y = 40 with a
    // -34..96 box, which put it below everything it was meant to shoot: an
    // aim-bot flown against it drifted to y = -24 and stayed there, which is
    // the tell that the resting position was in the wrong place.
    SHIP_Y_MIN: -70,
    SHIP_Y_MAX: 84,
    SHIP_Y_START: 10,
    SHIP_ACCEL: 0.85,
    SHIP_DRAG: 0.86,     // per-frame velocity multiplier
    SHIP_SPEED_MAX: 5.4,
    SHIP_W: 26,
    SHIP_H: 14,
    BANK_MAX: 1,         // |bank| at full lateral speed, drives the sprite roll

    MAX_LIVES: 3,
    INVINCIBLE_FRAMES: 100,

    // ── Guns ──────────────────────────────────────────────────────────
    SHOT_COOLDOWN: 9,
    SHOT_SPEED: 26,      // z units per frame, away from the camera
    // Hit boxes are a BOX per sprite, not one radius for everything, and the
    // margins deliberately favour the player in both directions:
    //
    //   a hostile is easier to hit than it looks   (+7 / +6)
    //   a convoy is exactly as big as it looks     (+0)
    //
    // The first cut had this backwards. It tested one circular radius, 11 for a
    // hostile and 15 for a convoy, so the thing you were aiming at was the
    // smaller target and the thing that costs you the run was the larger one.
    // Every near miss punished you twice: the hostile survived and the convoy
    // behind it took the shot.
    HOSTILE_HIT_MARGIN_W: 7,
    HOSTILE_HIT_MARGIN_H: 6,
    AID_HIT_MARGIN_W: 0,
    AID_HIT_MARGIN_H: 0,

    // Shots do nothing beyond this depth. It sits well inside the window where
    // every identification channel is already legible, so there is no such
    // thing as a shot you were not given the information to hold.
    Z_FIRE_MAX: 600,

    // ── Contacts ──────────────────────────────────────────────────────
    HOSTILE_W: 22,
    HOSTILE_H: 16,
    AID_W: 34,
    AID_H: 20,

    // Half-height of the band contacts spawn into. Wide enough that they use
    // the frame vertically rather than filing along the horizon, and inside the
    // ship's own Y box so everything that spawns can be reached.
    CONTACT_Y_SPREAD: 52,

    // Lateral drift, in world units per frame, while closing.
    HOSTILE_DRIFT: 0.9,
    AID_DRIFT: 0.32,

    // The transponder. Aid convoys squawk a steady double-blink; hostiles are
    // dark. Drawn at a constant screen size at every depth, so this channel is
    // readable from the frame a contact spawns — unlike silhouette or colour.
    // BLINK_PERIOD is the full cycle in 60fps frames: 30 = 2Hz.
    BLINK_PERIOD: 30,
    BLINK_ON_1: 6,       // frames 0..6 lit
    BLINK_ON_2: 14,      // and 10..14 lit — a double-tap, not a plain pulse
    BLINK_GAP: 10,

    // Fairness budget. A convoy must complete two full squawk cycles — 60
    // frames at BLINK_PERIOD 30 — before it can possibly be shot, plus a human
    // reaction allowance. Z_FAR and Z_FIRE_MAX are sized from this rather than
    // the other way round:
    //
    //     (Z_FAR - Z_FIRE_MAX) / RAIL_SPEED_MAX  >=  60 + 18
    //     (1100  -        600) /            6.0  =  83.3  >=  78     ok
    //
    // Widen the gap or slow the rail if either end is retuned; the test
    // asserts this rather than trusting it.
    TELEGRAPH_MIN_FRAMES: 60,
    REACTION_FRAMES: 18,

    // Depth at which an aid convoy's transponder is HEARD as well as seen
    // (sfx.js ping()). Well outside Z_FIRE_MAX on purpose: the point of the
    // sound is to tell you a convoy is inbound while you still have every
    // option, including the option to stop shooting.
    Z_PING: 900,

    // Silhouettes become readable around here; colour is the last channel and
    // never the only one.
    Z_SHAPE_READABLE: 410,

    // ── Spawning ──────────────────────────────────────────────────────
    // *_EARLY / *_LATE pairs are interpolated by difficulty in `Difficulty`.
    SPAWN_INTERVAL_EARLY: 84,
    SPAWN_INTERVAL_LATE: 32,

    // Two from the very first wave, not one. A wave of one is a wave that is
    // either a hostile or a convoy, never both, and the difference between
    // them is the only thing this game asks you to learn — so the opening
    // should show them side by side rather than one at a time a wave apart.
    WAVE_SIZE_EARLY: 2,
    WAVE_SIZE_LATE: 3,

    // Share of spawned contacts that are aid convoys. Deliberately flat: the
    // moral pressure must not thin out as the shooting gets busier.
    AID_SHARE_EARLY: 0.30,
    AID_SHARE_LATE: 0.26,

    // How readily hostiles break off to attack a convoy rather than the player.
    HOSTILE_AGGRO_EARLY: 0.25,
    HOSTILE_AGGRO_LATE: 0.75,

    // A hostile only locks a convoy it is actually near in depth. Without this
    // a hostile at the far end of the rail could mark a convoy at the near end,
    // and the line drawn between them crossed the whole screen pointing at
    // something the player could not yet reach.
    LOCK_MAX_DZ: 260,

    // A convoy under fire dies this many frames after a hostile locks it, which
    // is the window you have to kill the attacker.
    //
    // This is bounded from both sides and 105 was outside the lower one.
    //
    // FLOOR. Worst case the attacker is at the opposite corner of the rail:
    // 64 frames to fly there with the real Player.update, 23 for the shot to
    // reach maximum firing depth, 18 of reaction and up to 9 of gun cooldown,
    // so 114 frames before a perfect player can land the shot. At 105 the
    // convoy was already dead. It was not a tight window, it was an impossible
    // one, and the test now asserts the floor rather than trusting it.
    //
    // CEILING. A convoy has a median 125 frames of rail left when it is locked,
    // so a timer much past that means it reaches the camera and escapes before
    // the clock runs out, and the threat stops existing. Measured over a
    // 150-second passive run, the share of locked convoys that die:
    //
    //     105 -> 52%     150 -> 36%     180 -> 1%
    //     135 -> 42%     165 ->  4%
    //
    // The collapse between 150 and 165 is the timer overtaking the rail. 150 is
    // the largest value that still leaves the rule teeth, and it clears the
    // 114-frame floor by a comfortable 1.3x.
    AID_KILL_FRAMES: 150,

    // Minimum lateral separation between two contacts spawned in one wave, so a
    // convoy is never hidden behind a hostile at the moment you must identify it.
    SPAWN_MIN_SEPARATION: 62,
    SPAWN_X_RANGE: 150,

    // The cloud deck's bands recycle over their own depth span rather than
    // Z_FAR. Tied to Z_FAR they stopped well short of the horizon and left the
    // far third of the deck as a flat wall of colour; running them out to 3200
    // lets them converge into the haze the way the rails do.
    DECK_Z_SPAN: 3200,

    // ── Scoring ───────────────────────────────────────────────────────
    SCORE_HOSTILE: 100,
    SCORE_ESCORT: 500,
    SCORE_FRIENDLY_FIRE: -1000,
    COMBO_MAX: 6,
    COMBO_WINDOW: 180,   // frames before a streak lapses

    // Three friendly-fire hits end the run outright. Restraint is not optional
    // scoring advice — it is a losing condition, which is the whole point of
    // the title.
    MAX_STRIKES: 3,

    // ── Effects ───────────────────────────────────────────────────────
    PARTICLE_COUNT: 10,
    PARTICLE_LIFE: 24,
    PARTICLE_SPEED: 2.6,

    // ── Music ─────────────────────────────────────────────────────────
    //
    // Points at the jukebox's copy rather than a second one under this game:
    // the track is 2.7MB and is on the jukebox in its own right, so a local
    // copy would be pure duplication. Same trade Roderick Tron makes — the
    // game is not quite standalone, and that is the one convention it breaks.
    MUSIC: {
        URL: '../../music/jukebox/songs/'
             + encodeURIComponent('Jimmi - JIMMI - 02 The Jovian Humanitarian Conflict.ogg'),
        VOLUME: 0.42,
        FADE_IN: 1.5,
        FADE_OUT: 0.9,
    },

    // ── Palette — the Jovian cloud decks ──────────────────────────────
    //
    // Warm ammonia bands over a cold void. Aid amber against hostile magenta:
    // never red/green, and the two differ in luminance as well as hue so the
    // distinction survives being read in greyscale.
    COLORS: {
        void:         '#05060f',
        voidHaze:     '#0d1230',
        star:         '#c8d4ff',

        // Gas giant backdrop, light band to dark, plus the Spot.
        bandCream:    '#e8cfa0',
        bandTan:      '#c99a63',
        bandUmber:    '#8f5f3c',
        bandShadow:   '#5c3a28',
        bandDeep:     '#38222a',
        spotRed:      '#b4523f',
        spotCore:     '#8c3628',

        // The rail's cloud deck, receding.
        deckNear:     '#7a5a7e',
        deckFar:      '#2a2340',
        deckLine:     '#b489c4',

        hostile:      '#ff2fa8',
        hostileDark:  '#7a1150',
        hostileEye:   '#ffd0ec',

        aid:          '#ffc247',
        aidPale:      '#fff2cf',
        aidDark:      '#8a5f12',
        aidBeacon:    '#ffffff',

        shipHull:     '#dfe8ff',
        shipSteel:    '#7f90b8',
        shipShadow:   '#2b3350',
        shipGlass:    '#5ff0ff',
        thrust:       '#7ce8ff',
        thrustHot:    '#ffffff',

        shot:         '#9ffcff',
        shotCore:     '#ffffff',

        hudText:      '#e8eeff',
        hudDim:       '#6b7699',
        lifeIcon:     '#5ff0ff',
        strike:       '#ff2fa8',
        warn:         '#ff2fa8',
        particleHot:  '#ffd8a0',
        particleCool: '#7a6a92',
    },
};

/**
 * The single source of "how hard is it now".
 *
 * Every escalating quantity is derived here from one number, so tuning the
 * curve never means hunting for a second place that also reads the clock.
 * `t` is 0 at the start of a run and 1 once DIFFICULTY_DISTANCE is behind you.
 */
const Difficulty = {
    lerp(a, b, t) { return a + (b - a) * t; },

    at(distance) {
        return Math.min(1, Math.max(0, distance / CONFIG.DIFFICULTY_DISTANCE));
    },

    railSpeed(t)     { return this.lerp(CONFIG.RAIL_SPEED_MIN, CONFIG.RAIL_SPEED_MAX, t); },
    spawnInterval(t) { return this.lerp(CONFIG.SPAWN_INTERVAL_EARLY, CONFIG.SPAWN_INTERVAL_LATE, t); },
    aidShare(t)      { return this.lerp(CONFIG.AID_SHARE_EARLY, CONFIG.AID_SHARE_LATE, t); },
    aggro(t)         { return this.lerp(CONFIG.HOSTILE_AGGRO_EARLY, CONFIG.HOSTILE_AGGRO_LATE, t); },

    /** Contacts released together. Fractional part is a per-wave coin flip. */
    waveSize(t, rand) {
        const raw = this.lerp(CONFIG.WAVE_SIZE_EARLY, CONFIG.WAVE_SIZE_LATE, t);
        const base = Math.floor(raw);
        return base + ((rand || Math.random)() < raw - base ? 1 : 0);
    },

    /**
     * Frames a contact spends between spawning and entering firing range, at
     * the given difficulty. This is the identification budget, and the reason
     * RAIL_SPEED_MAX is capped where it is.
     */
    identifyFrames(t) {
        return (CONFIG.Z_FAR - CONFIG.Z_FIRE_MAX) / this.railSpeed(t);
    },
};
