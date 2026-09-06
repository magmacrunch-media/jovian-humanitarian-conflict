// sfx.js — The Jovian Humanitarian Conflict | MagmaCrunch Media © 2026
// Procedural sound effects, synthesised rather than shipped.
//
// No .ogg files: every sound here is a few oscillators and a noise burst, the
// way arcade/very-long-boards does it. That keeps the game to one audio asset
// (the track itself, already 2.7MB) and lets the sounds be tuned by editing
// numbers instead of re-exporting samples.
//
// AdAudio is deliberately not used for these. It loads SFX from URLs into
// buffers, which is the right shape for recorded samples and the wrong one for
// six sounds that are cheaper to generate than to fetch. AdAudio still owns the
// music, and this module owns everything else.
//
// ── The ping is not decoration ──
//
// `ping()` is the transponder heard rather than seen: it fires once per aid
// convoy, at Z_PING, which is well outside firing range. It exists so that a
// player who misses the visual blink still gets told a convoy is inbound, and
// it deliberately copies the rhythm of that blink — two quick notes at the same
// spacing — so the eye and the ear are reporting the same thing rather than two
// unrelated signals. Everything else in here can be muted without changing what
// the game asks of you. This one cannot, which is why it is the loudest of the
// quiet sounds.

const SFX = {
    ctx: null,
    muted: false,
    ready: false,

    /**
     * Build the context, and arrange for the first user gesture to resume it.
     *
     * A browser starts an AudioContext 'suspended' and only a gesture may
     * resume it, so this is called at load and the listeners do the rest. They
     * remove themselves, since after the first one there is nothing to do.
     */
    init() {
        try {
            const Ctor = window.AudioContext || window.webkitAudioContext;
            if (!Ctor) return;
            this.ctx = new Ctor();
            this.ready = true;

            const resume = () => {
                if (this.ctx && this.ctx.state === 'suspended') this.ctx.resume();
                document.removeEventListener('keydown', resume);
                document.removeEventListener('pointerdown', resume);
            };
            document.addEventListener('keydown', resume);
            document.addEventListener('pointerdown', resume);
        } catch (e) {
            this.ready = false;
        }
    },

    setMuted(m) {
        this.muted = !!m;
    },

    /** Nothing plays while muted, or while the tab is in the background. */
    _live() {
        return this.ready && this.ctx && !this.muted && !document.hidden;
    },

    /**
     * One enveloped oscillator.
     *
     * The gain ramp starts from a tiny positive value because
     * exponentialRampToValueAtTime cannot approach zero, and a linear ramp on a
     * 60ms blip clicks audibly at this sample rate.
     */
    tone(freq, dur, type, vol, freqTo, delay) {
        if (!this._live()) return;
        const t0 = this.ctx.currentTime + (delay || 0);
        const osc = this.ctx.createOscillator();
        const gain = this.ctx.createGain();

        osc.type = type || 'square';
        osc.frequency.setValueAtTime(freq, t0);
        if (freqTo) osc.frequency.exponentialRampToValueAtTime(freqTo, t0 + dur);

        gain.gain.setValueAtTime(0.0001, t0);
        gain.gain.exponentialRampToValueAtTime(vol, t0 + 0.008);
        gain.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);

        osc.connect(gain);
        gain.connect(this.ctx.destination);
        osc.start(t0);
        osc.stop(t0 + dur + 0.02);
    },

    /**
     * A burst of filtered white noise, for anything that breaks up.
     *
     * The buffer is built per call rather than cached: at these durations it is
     * a few thousand samples, and a fresh one keeps two explosions in the same
     * frame from sounding like one doubled in volume.
     */
    noise(dur, vol, cutoff, delay) {
        if (!this._live()) return;
        const t0 = this.ctx.currentTime + (delay || 0);
        const n = Math.max(1, Math.floor(this.ctx.sampleRate * dur));
        const buf = this.ctx.createBuffer(1, n, this.ctx.sampleRate);
        const data = buf.getChannelData(0);
        for (let i = 0; i < n; i++) data[i] = Math.random() * 2 - 1;

        const src = this.ctx.createBufferSource();
        src.buffer = buf;

        const filt = this.ctx.createBiquadFilter();
        filt.type = 'lowpass';
        filt.frequency.setValueAtTime(cutoff || 1800, t0);
        filt.frequency.exponentialRampToValueAtTime(180, t0 + dur);

        const gain = this.ctx.createGain();
        gain.gain.setValueAtTime(vol, t0);
        gain.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);

        src.connect(filt);
        filt.connect(gain);
        gain.connect(this.ctx.destination);
        src.start(t0);
        src.stop(t0 + dur + 0.02);
    },

    // ── The sounds ────────────────────────────────────────────────────
    //
    // Levels are set against each other, not in isolation. shoot() fires up to
    // seven times a second and sits under everything; ping() has to stay
    // audible through it, because it is carrying information.

    /** Firing. Deliberately thin, so a held trigger does not bury the ping. */
    shoot() {
        this.tone(880, 0.07, 'square', 0.035, 260);
    },

    /** A hostile breaking up. */
    explode() {
        this.noise(0.26, 0.16, 2600);
        this.tone(180, 0.20, 'sawtooth', 0.09, 60);
    },

    /**
     * The transponder, heard. Two notes at the spacing of the visual blink.
     *
     * BLINK_GAP is 10 frames of 60, so the second note lands 1/6 of a second
     * after the first, matching what the eye is being shown.
     */
    ping() {
        const gap = CONFIG.BLINK_GAP / 60;
        this.tone(1760, 0.05, 'sine', 0.075);
        this.tone(1760, 0.06, 'sine', 0.075, null, gap);
    },

    /** A convoy escorted clear. Small rising third, warm. */
    escort() {
        this.tone(660, 0.09, 'sine', 0.075);
        this.tone(880, 0.09, 'sine', 0.065, null, 0.08);
        this.tone(1320, 0.14, 'sine', 0.05, null, 0.16);
    },

    /**
     * Friendly fire. The loudest thing in the game, and the only harsh one.
     *
     * A two-tone descending sawtooth over the explosion, which is the shape of
     * an alarm rather than a hit. It should be unpleasant: it is the sound of
     * the thing the whole game is about going wrong, and a player who hears it
     * twice should already know what it means.
     */
    friendlyFire() {
        this.noise(0.3, 0.14, 2200);
        this.tone(440, 0.22, 'sawtooth', 0.20, 300);
        this.tone(330, 0.30, 'sawtooth', 0.18, 200, 0.20);
    },

    /**
     * A convoy lost to hostiles. Related to friendly fire but not the same:
     * you failed to prevent it rather than caused it, and the sound says so by
     * falling away instead of sounding an alarm.
     */
    lost() {
        this.noise(0.24, 0.10, 1400);
        this.tone(300, 0.34, 'triangle', 0.10, 120);
    },

    /**
     * A convoy has been marked and is now on a clock.
     *
     * Rising rather than falling, and distinct from ping(): the ping says a
     * convoy exists, this says one is about to die unless you do something.
     * The two get confused if they share a shape, so they share nothing.
     */
    lock() {
        this.tone(520, 0.08, 'triangle', 0.11, 900);
        this.tone(700, 0.10, 'triangle', 0.10, 1200, 0.09);
    },

    /** The player taking a hit. */
    playerHit() {
        this.noise(0.22, 0.15, 1200);
        this.tone(140, 0.26, 'square', 0.14, 50);
    },
};
