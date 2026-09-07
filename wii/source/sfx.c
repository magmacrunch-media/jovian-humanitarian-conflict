/* =====================================================================
 * sfx.c -- The Jovian Humanitarian Conflict (Wii)
 * See sfx.h. A port of web/js/sfx.js, oscillator for oscillator.
 *
 * NOTHING in this translation unit may include grrlib.h, ogc/ or magnolia.h.
 * ===================================================================== */
#include <math.h>
#include <string.h>

#include "config.h"
#include "sfx.h"

/* -- Master level -----------------------------------------------------
 *
 * The browser's numbers are gains into an AudioContext destination and run
 * 0.035 to 0.20. They are carried across UNCHANGED and scaled by one constant
 * here, because what matters is the ratios between them, not their absolute
 * size: sfx.js sets its levels against each other and says so -- shoot() fires
 * up to seven times a second and must sit under everything, ping() has to stay
 * audible through it because it is carrying information.
 *
 * Retune by editing the per-sound numbers below, which are the browser's, and
 * carrying the change to web/js/sfx.js. Retune the overall loudness by editing
 * this one number, which is nobody's but ours.
 *
 * ---------------------------------------------------------------------
 * It is 1.45, and it was 2.4, and the difference is the music.
 *
 * 2.4 put the loudest effect just under full scale ON ITS OWN, which passed a
 * suite that only knew how to render one effect at a time -- and then ASND
 * summed it with a music bed and two other effects and hit the rail. Measured
 * off Dolphin's audio dump: 0.9% of samples clipped, in runs up to 64 samples,
 * which is 1.33ms of flat-topped waveform and audible as a crack on exactly
 * the loud moments the game cares about.
 *
 * An effect has to fit in what the music leaves, ALONGSIDE the other effects
 * playing with it. Measured, the music peaks around 0.30 of full scale, and the
 * four that plausibly overlap are a kill, an escort, the gun and a ping. 1.1
 * puts that sum under the rail with room to spare; 1.45 did not, and dropping
 * the effects 40% from 2.4 removed only a tenth of the clipping, because the
 * problem was never one effect being loud -- it was six of them at once.
 *
 * The suite asserts both halves now: each effect fits the headroom the music
 * leaves, and the concurrent four fit together.
 */
#define MASTER 0.9f

/* -- A deterministic noise source -------------------------------------
 * Its own generator rather than the simulation's: the two must not share a
 * stream, or rendering a sound would move the spawn sequence. Same mulberry32
 * arithmetic, seeded per effect so a render is reproducible.
 */
static unsigned int g_noise;

static void noise_seed(unsigned int s) { g_noise = s; }

static float noise_next(void) {
    unsigned int a = g_noise + 0x6D2B79F5u;
    unsigned int t;
    g_noise = a;
    t = (a ^ (a >> 15)) * (1u | a);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)((double)(t ^ (t >> 14)) / 4294967296.0) * 2.0f - 1.0f;
}

/* -- Envelopes ---------------------------------------------------------
 *
 * WebAudio's exponentialRampToValueAtTime, which is what sfx.js uses for every
 * envelope: v(t) = v0 * (v1/v0)^(t/T). It cannot approach zero, which is why
 * the browser ramps to 0.0001 rather than 0 -- and why a linear ramp was
 * rejected there: on a 60ms blip it clicks audibly. Both properties are worth
 * keeping, so this is the same curve and not a cheaper approximation.
 */
static float expramp(float v0, float v1, float t, float dur) {
    if (dur <= 0.0f) return v1;
    if (t <= 0.0f) return v0;
    if (t >= dur) return v1;
    return v0 * powf(v1 / v0, t / dur);
}

#define EPS 0.0001f

typedef enum { W_SINE = 0, W_SQUARE, W_SAW, W_TRI } Wave;

/* Phase in [0,1) -> sample in [-1,1].
 *
 * The triangle differs from WebAudio's by a quarter cycle -- theirs starts at
 * zero rising, this starts at the peak. On a 90ms blip that is inaudible, and
 * matching it exactly would cost a phase offset on the one waveform whose
 * shape nobody can pick out of a two-note chirp. */
static float wave(Wave w, float p) {
    switch (w) {
    case W_SQUARE: return p < 0.5f ? 1.0f : -1.0f;
    case W_SAW:    return 2.0f * p - 1.0f;
    case W_TRI:    return 4.0f * fabsf(p - 0.5f) - 1.0f;
    case W_SINE:
    default:       return sinf(6.28318530718f * p);
    }
}

/* -- The two primitives, mixed additively into `out` -------------------- */

/*
 * One enveloped oscillator, optionally sweeping in frequency.
 *
 * The sweep is exponential, as in the browser, and the phase is accumulated
 * from the instantaneous frequency rather than computed from a start frequency
 * -- integrating the sweep rather than ignoring it. Getting that wrong is
 * inaudible on a short blip and wrong by an octave on a long one, and
 * friendlyFire's 0.30s fall is long enough to notice.
 */
static void tone(float *acc, int n, float freq, float dur, Wave w, float vol,
                 float freq_to, float delay) {
    int i0 = (int)(delay * SFX_RATE);
    int len = (int)(dur * SFX_RATE);
    float phase = 0.0f;
    int i;

    for (i = 0; i < len; i++) {
        int at = i0 + i;
        float t, f, env;
        if (at < 0 || at >= n) break;
        t = (float)i / (float)SFX_RATE;

        f = freq_to > 0.0f ? expramp(freq, freq_to, t, dur) : freq;
        phase += f / (float)SFX_RATE;
        while (phase >= 1.0f) phase -= 1.0f;

        /* 8ms attack, then decay across the rest -- sfx.js exactly. */
        if (t < 0.008f) env = expramp(EPS, vol, t, 0.008f);
        else            env = expramp(vol, EPS, t - 0.008f, dur - 0.008f);

        acc[at] += wave(w, phase) * env;
    }
}

/*
 * A burst of filtered white noise, for anything that breaks up.
 *
 * The browser sweeps a biquad lowpass from `cutoff` down to 180Hz across the
 * burst. This is a one-pole lowpass doing the same sweep: 6dB/octave against
 * the biquad's 12, so the tail is a shade brighter. On a TV speaker, under an
 * explosion, that is not a difference anybody can hear -- and a one-pole has
 * no resonance to blow up when the cutoff sweeps two decades in a quarter of a
 * second, which a badly-conditioned biquad very much does.
 */
static void noise_burst(float *acc, int n, float dur, float vol, float cutoff,
                        float delay) {
    int i0 = (int)(delay * SFX_RATE);
    int len = (int)(dur * SFX_RATE);
    float lp = 0.0f;
    int i;

    for (i = 0; i < len; i++) {
        int at = i0 + i;
        float t, fc, k, env;
        if (at < 0 || at >= n) break;
        t = (float)i / (float)SFX_RATE;

        fc = expramp(cutoff, 180.0f, t, dur);
        k = 1.0f - expf(-6.28318530718f * fc / (float)SFX_RATE);
        lp += (noise_next() - lp) * k;

        env = expramp(vol, EPS, t, dur);
        acc[at] += lp * env;
    }
}

/* -- The sounds --------------------------------------------------------
 *
 * Levels and durations are web/js/sfx.js's, unchanged. A change here is a
 * change there, and the commit should say it was made in both.
 */
static int build(SfxId id, float *acc, int n) {
    switch (id) {
    /* Firing. Deliberately thin, so a held trigger does not bury the ping. */
    case SFX_SHOOT:
        tone(acc, n, 880.0f, 0.07f, W_SQUARE, 0.035f, 260.0f, 0.0f);
        return (int)(0.09f * SFX_RATE);

    /* A hostile breaking up. */
    case SFX_EXPLODE:
        noise_burst(acc, n, 0.26f, 0.16f, 2600.0f, 0.0f);
        tone(acc, n, 180.0f, 0.20f, W_SAW, 0.09f, 60.0f, 0.0f);
        return (int)(0.28f * SFX_RATE);

    /* The transponder, heard. Two notes at the spacing of the visual blink:
     * BLINK_GAP frames of 60, so the second lands 1/6 of a second after the
     * first, matching what the eye is being shown. The suite asserts that
     * relationship against the constant rather than against 0.167. */
    case SFX_PING: {
        float gap = (float)BLINK_GAP / 60.0f;
        tone(acc, n, 1760.0f, 0.05f, W_SINE, 0.075f, 0.0f, 0.0f);
        tone(acc, n, 1760.0f, 0.06f, W_SINE, 0.075f, 0.0f, gap);
        return (int)((gap + 0.08f) * SFX_RATE);
    }

    /* A convoy escorted clear. Small rising third, warm. */
    case SFX_ESCORT:
        tone(acc, n, 660.0f,  0.09f, W_SINE, 0.075f, 0.0f, 0.0f);
        tone(acc, n, 880.0f,  0.09f, W_SINE, 0.065f, 0.0f, 0.08f);
        tone(acc, n, 1320.0f, 0.14f, W_SINE, 0.05f,  0.0f, 0.16f);
        return (int)(0.32f * SFX_RATE);

    /* Friendly fire. The loudest thing in the game, and the only harsh one.
     * A two-tone descending sawtooth over the explosion, which is the shape of
     * an alarm rather than a hit. It should be unpleasant: it is the sound of
     * the thing the whole game is about going wrong. */
    case SFX_FRIENDLY_FIRE:
        noise_burst(acc, n, 0.30f, 0.14f, 2200.0f, 0.0f);
        tone(acc, n, 440.0f, 0.22f, W_SAW, 0.20f, 300.0f, 0.0f);
        tone(acc, n, 330.0f, 0.30f, W_SAW, 0.18f, 200.0f, 0.20f);
        return (int)(0.52f * SFX_RATE);

    /* A convoy lost to hostiles. Related to friendly fire but not the same:
     * you failed to prevent it rather than caused it, and the sound says so by
     * falling away instead of sounding an alarm. */
    case SFX_LOST:
        noise_burst(acc, n, 0.24f, 0.10f, 1400.0f, 0.0f);
        tone(acc, n, 300.0f, 0.34f, W_TRI, 0.10f, 120.0f, 0.0f);
        return (int)(0.36f * SFX_RATE);

    /* A convoy has been marked and is now on a clock. Rising rather than
     * falling, and distinct from the ping: the ping says a convoy exists, this
     * says one is about to die unless you do something. The two get confused if
     * they share a shape, so they share nothing. */
    case SFX_LOCK:
        tone(acc, n, 520.0f, 0.08f, W_TRI, 0.11f, 900.0f,  0.0f);
        tone(acc, n, 700.0f, 0.10f, W_TRI, 0.10f, 1200.0f, 0.09f);
        return (int)(0.21f * SFX_RATE);

    /* The player taking a hit. */
    case SFX_PLAYER_HIT:
        noise_burst(acc, n, 0.22f, 0.15f, 1200.0f, 0.0f);
        tone(acc, n, 140.0f, 0.26f, W_SQUARE, 0.14f, 50.0f, 0.0f);
        return (int)(0.28f * SFX_RATE);

    default:
        return 0;
    }
}

int sfx_render(SfxId id, short *out, int max_samples) {
    /* One float accumulator, so overlapping oscillators sum before they are
     * quantised -- mixing in int16 would clip on the way in and there would be
     * nothing left to detect it with. */
    static float acc[SFX_MAX_SAMPLES];
    int n, len, i;

    if (id < 0 || id >= SFX_COUNT || !out || max_samples <= 0) return 0;

    n = max_samples < SFX_MAX_SAMPLES ? max_samples : SFX_MAX_SAMPLES;
    memset(acc, 0, sizeof(float) * (size_t)n);

    /* Seeded per effect, so each has its own noise and every render of it is
     * the same render. */
    noise_seed(0x4A4F5600u + (unsigned int)id);

    len = build(id, acc, n);
    if (len <= 0) return 0;
    if (len > n) len = n;

    for (i = 0; i < len; i++) {
        float v = acc[i] * MASTER * 32767.0f;
        /* Clamped rather than wrapped. A wrap turns an overshoot into a burst
         * of noise that sounds like a broken speaker; a clamp merely sounds
         * loud. The suite asserts we never reach either. */
        if (v >  32767.0f) v =  32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        out[i] = (short)v;
    }
    return len;
}

int sfx_peak(SfxId id, short *scratch, int max_samples) {
    int len = sfx_render(id, scratch, max_samples);
    int i, peak = 0;
    for (i = 0; i < len; i++) {
        int v = scratch[i] < 0 ? -scratch[i] : scratch[i];
        if (v > peak) peak = v;
    }
    return peak;
}
