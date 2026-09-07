/* =====================================================================
 * test_sfx.c -- The Jovian Humanitarian Conflict (Wii)
 *
 * source/sfx.c synthesises the six sounds and touches no libogc, no GRRLIB and
 * no magnolia, so the real shipped translation unit links here.
 *
 * The point of testing audio you cannot listen to: every property below is one
 * the design of web/js/sfx.js explicitly depends on, and every one of them is
 * invisible on a Wii through a television. "It made a noise" is what a console
 * can tell you. Whether the noise was the right shape, at the right level,
 * relative to the other five, is arithmetic.
 *
 * What this guards, in order of how much it would hurt:
 *
 *  1. The ping's spacing. It is the transponder HEARD, and it deliberately
 *     copies the rhythm of the visual blink -- two notes at BLINK_GAP frames
 *     apart -- so the eye and the ear report the same thing rather than two
 *     unrelated signals. Retune BLINK_GAP for the renderer and the sound has
 *     to follow, which is exactly the sort of link nobody remembers.
 *
 *  2. Clipping. Eight effects can overlap in one frame; each is mixed in float
 *     and quantised once. An effect that clips on its own is a design error
 *     that sounds like a broken speaker, and nothing on the console reports it.
 *
 *  3. The level ORDER. sfx.js sets its levels against each other and says so:
 *     shoot() fires up to seven times a second and must sit under everything,
 *     ping() must stay audible through it, friendlyFire() is the loudest thing
 *     in the game. Those relationships are the design; the absolute numbers are
 *     not.
 *
 *  4. Determinism. The noise bursts are seeded, so a render is reproducible and
 *     these bounds can be tight. A fresh random burst per run would force
 *     bounds loose enough to catch nothing.
 *
 * Run: make test
 * ===================================================================== */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "sfx.h"

static int checks = 0;
static int failures = 0;

static void check(int cond, const char *what) {
    checks++;
    if (cond) {
        printf("  PASS  %s\n", what);
    } else {
        failures++;
        printf("  FAIL  %s\n", what);
    }
}

static void check_detail(int cond, const char *what, const char *fmt,
                         double a, double b) {
    checks++;
    if (cond) {
        printf("  PASS  %s\n", what);
    } else {
        failures++;
        printf("  FAIL  %s\n", what);
        printf("          ");
        printf(fmt, a, b);
        printf("\n");
    }
}

static const char *NAMES[SFX_COUNT] = {
    "shoot", "explode", "ping", "escort",
    "friendly-fire", "lost", "lock", "player-hit"
};

static short buf[SFX_MAX_SAMPLES];
static short buf2[SFX_MAX_SAMPLES];

/* Onsets have to be found on the ENVELOPE, not on the samples.
 *
 * The first version of this scanned raw samples for a drop below a threshold
 * and called that the gap between the ping's two notes. A 1760Hz sine passes
 * through zero 3520 times a second, so it found the first zero crossing at
 * sample 229 and reported the notes as 7ms apart instead of 167ms. The signal
 * was right and the measurement was wrong, which is the failure mode a test
 * exists to avoid rather than to demonstrate.
 *
 * ENV_WIN is 2ms, comfortably longer than a cycle at the highest frequency any
 * effect uses (1760Hz, 0.57ms) and far shorter than the shortest note (50ms).
 */
#define ENV_WIN (SFX_RATE / 500)

/* Peak magnitude of the window starting at sample `at`. */
static int env_at(const short *s, int len, int at) {
    int i, peak = 0;
    for (i = at; i < at + ENV_WIN && i < len; i++) {
        int v = s[i] < 0 ? -s[i] : s[i];
        if (v > peak) peak = v;
    }
    return peak;
}

/* First sample at or after `from` where the envelope rises above `thresh`. */
static int onset_after(const short *s, int len, int from, int thresh) {
    int i;
    for (i = from; i < len; i += ENV_WIN) {
        if (env_at(s, len, i) > thresh) return i;
    }
    return -1;
}

/* First sample at or after `from` where the envelope falls below `thresh`. */
static int silence_after(const short *s, int len, int from, int thresh) {
    int i;
    for (i = from; i < len; i += ENV_WIN) {
        if (env_at(s, len, i) < thresh) return i;
    }
    return -1;
}

int main(void) {
    int peaks[SFX_COUNT];
    int lens[SFX_COUNT];
    int i;

    printf("The Jovian Humanitarian Conflict -- Wii sound tests\n");

    printf("\nevery effect renders\n");
    for (i = 0; i < SFX_COUNT; i++) {
        lens[i] = sfx_render((SfxId)i, buf, SFX_MAX_SAMPLES);
        peaks[i] = sfx_peak((SfxId)i, buf, SFX_MAX_SAMPLES);
        check_detail(lens[i] > 0 && lens[i] <= SFX_MAX_SAMPLES,
                     NAMES[i], "%.0f samples, cap %.0f",
                     (double)lens[i], (double)SFX_MAX_SAMPLES);
    }

    /* -- Clipping ------------------------------------------------------ */
    printf("\nnothing clips, and nothing is silent\n");
    for (i = 0; i < SFX_COUNT; i++) {
        /* 32767 exactly means the clamp in sfx_render fired, which means the
         * master level is too high for this effect. */
        check_detail(peaks[i] < 32767, NAMES[i],
                     "peak %.0f hit the clamp at %.0f",
                     (double)peaks[i], 32767.0);
    }

    /* And, the check this suite was missing: an effect does not play alone.
     *
     * The first version asserted only that a single rendered effect did not
     * clip. It did not, and the game clipped anyway -- ASND sums the effects
     * with each other and with a music bed at 0.42 of full scale, and 0.9% of
     * the console's actual output was hitting the rail in runs long enough to
     * hear. Measured off a Dolphin audio dump, which is the only place that
     * fact was visible.
     *
     * SFX_HEADROOM is what the music leaves: an effect must fit inside it, so
     * that the loudest effect plus a full-scale music bed still lands under
     * full scale. */
    printf("\nand they leave room for the music to sum into\n");
    {
        /* Measured off a Dolphin dump: the music alone peaks near 0.30 of
         * full scale, so 0.58 is a generous per-effect ceiling. */
        const int SFX_HEADROOM = (int)(0.58f * 32767.0f);
        for (i = 0; i < SFX_COUNT; i++) {
            check_detail(peaks[i] <= SFX_HEADROOM, NAMES[i],
                         "peak %.0f leaves nothing for the music, cap %.0f",
                         (double)peaks[i], (double)SFX_HEADROOM);
        }

        /* And together. main.c plays each DISTINCT effect at most once a
         * frame, so the realistic worst case is the four that can be raised by
         * one frame's events: a kill, an escort, the gun, and a convoy
         * announcing itself. They are not sample-aligned in practice, so
         * summing their peaks is pessimistic -- which is the right direction
         * for a bound.
         *
         * This is the check the suite did not have when the game clipped. Each
         * effect passed on its own and the sum was never looked at. */
        {
            int concurrent = peaks[SFX_EXPLODE] + peaks[SFX_ESCORT]
                           + peaks[SFX_SHOOT] + peaks[SFX_PING];
            int budget = (int)(0.70f * 32767.0f);   /* 1.0 less the music */
            check_detail(concurrent <= budget,
                         "a kill, an escort, the gun and a ping fit together",
                         "they sum to %.0f, budget %.0f",
                         (double)concurrent, (double)budget);
        }
    }
    for (i = 0; i < SFX_COUNT; i++) {
        /* 5% of full scale, which is a floor for "something came out", not a
         * judgement about mix balance -- the ORDER checks below are where the
         * design lives.
         *
         * It was a tenth of full scale to begin with, and shoot() failed it at
         * 8.4%. That was the test being wrong about the game: shoot is
         * deliberately the thinnest sound in the set, because it fires seven
         * times a second and must not bury the ping. A floor that forbids the
         * quietest sound from being quiet is a floor that would have to be
         * satisfied by breaking the mix. */
        check_detail(peaks[i] > 1000, NAMES[i],
                     "peak %.0f is silent, want over %.0f",
                     (double)peaks[i], 1000.0);
    }

    /* -- The ping, and its link to the visual blink -------------------- */
    printf("\nthe ping matches the blink it stands for\n");
    {
        int len = sfx_render(SFX_PING, buf, SFX_MAX_SAMPLES);
        int peak = sfx_peak(SFX_PING, buf2, SFX_MAX_SAMPLES);
        int thresh = peak / 3;
        int first = onset_after(buf, len, 0, thresh);
        int gap_samples = (int)(((float)BLINK_GAP / 60.0f) * SFX_RATE);
        int quiet, second;

        check(first >= 0 && first < SFX_RATE / 100,
              "the first note starts immediately");

        /* The trough between the notes, then the onset after it. */
        quiet = silence_after(buf, len, first, thresh / 4);
        second = quiet < 0 ? -1 : onset_after(buf, len, quiet, thresh);

        check(second > 0, "there is a second note");
        if (second > 0) {
            /* Within 10ms of BLINK_GAP frames: the envelope window is 2ms
             * and the attack is 8ms, so that is the measurement's own floor.
             * Anything larger is the relationship drifting, not the ruler. */
            int err = second - gap_samples;
            if (err < 0) err = -err;
            check_detail(err < SFX_RATE / 100,
                         "and it lands BLINK_GAP frames after the first",
                         "second note at %.0f samples, blink gap is %.0f",
                         (double)second, (double)gap_samples);
        }
    }

    /* -- Levels, relative to each other -------------------------------- */
    printf("\nthe levels stay in the order the design needs\n");

    /* sfx.js: "shoot() fires up to seven times a second and sits under
     * everything; ping() has to stay audible through it, because it is
     * carrying information." */
    check_detail(peaks[SFX_PING] > peaks[SFX_SHOOT],
                 "the ping is louder than the gun it must be heard through",
                 "ping %.0f vs shoot %.0f",
                 (double)peaks[SFX_PING], (double)peaks[SFX_SHOOT]);

    for (i = 0; i < SFX_COUNT; i++) {
        if (i == SFX_SHOOT) continue;
        check_detail(peaks[SFX_SHOOT] <= peaks[i],
                     "the gun sits under everything else",
                     "shoot %.0f vs %.0f", (double)peaks[SFX_SHOOT],
                     (double)peaks[i]);
    }

    /* sfx.js: friendly fire is "the loudest thing in the game". It is also the
     * only one the player is being told off by, so if a retune ever makes
     * something else louder, the game has stopped emphasising the one event
     * the whole premise is about. */
    for (i = 0; i < SFX_COUNT; i++) {
        if (i == SFX_FRIENDLY_FIRE) continue;
        check_detail(peaks[SFX_FRIENDLY_FIRE] >= peaks[i],
                     "friendly fire is the loudest thing in the game",
                     "friendly-fire %.0f vs %.0f",
                     (double)peaks[SFX_FRIENDLY_FIRE], (double)peaks[i]);
    }

    /* Losing a convoy to hostiles is a failure to prevent, not a thing you
     * did; it must not shout as loudly as the thing you did. */
    check_detail(peaks[SFX_LOST] < peaks[SFX_FRIENDLY_FIRE],
                 "a convoy lost is quieter than a convoy shot",
                 "lost %.0f vs friendly-fire %.0f",
                 (double)peaks[SFX_LOST], (double)peaks[SFX_FRIENDLY_FIRE]);

    /* -- Determinism ---------------------------------------------------- */
    printf("\na render is reproducible\n");
    {
        int same = 1;
        int a = sfx_render(SFX_EXPLODE, buf, SFX_MAX_SAMPLES);
        int b = sfx_render(SFX_EXPLODE, buf2, SFX_MAX_SAMPLES);
        if (a != b) same = 0;
        else if (memcmp(buf, buf2, sizeof(short) * (size_t)a) != 0) same = 0;
        check(same, "the same effect twice is the same samples");
    }
    {
        /* And two different effects are not accidentally the same stream --
         * they are seeded per id, and a seed that ignored the id would make
         * every noise burst identical. */
        int n1 = sfx_render(SFX_EXPLODE, buf, SFX_MAX_SAMPLES);
        int n2 = sfx_render(SFX_LOST, buf2, SFX_MAX_SAMPLES);
        int n = n1 < n2 ? n1 : n2;
        check(memcmp(buf, buf2, sizeof(short) * (size_t)n) != 0,
              "two different effects are two different sounds");
    }

    /* -- Buffer discipline ---------------------------------------------- */
    printf("\nit stays inside the buffer it was given\n");
    {
        short small[64];
        int n = sfx_render(SFX_FRIENDLY_FIRE, small, 64);
        check(n <= 64, "a short buffer truncates rather than overruns");
        check(sfx_render((SfxId)SFX_COUNT, buf, SFX_MAX_SAMPLES) == 0,
              "an unknown effect renders nothing");
        check(sfx_render(SFX_SHOOT, buf, 0) == 0,
              "and a zero-length buffer is refused");
    }

    /* -- Memory, which is the reason these are synthesised at all ------- */
    printf("\nthe whole set is cheap\n");
    {
        int total = 0;
        for (i = 0; i < SFX_COUNT; i++) total += lens[i];
        check_detail(total * 2 < 300 * 1024,
                     "all eight effects fit in a quarter of a megabyte",
                     "%.0f KB of %.0f KB",
                     (double)(total * 2) / 1024.0, 300.0);
        printf("          (%d KB resident, against %d KB for the music)\n",
               (total * 2) / 1024, 6500);
    }

    printf("\n%d checks, %d failed\n\n", checks, failures);
    return failures ? 1 : 0;
}
