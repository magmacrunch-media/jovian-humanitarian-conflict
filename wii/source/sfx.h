/* =====================================================================
 * sfx.h -- The Jovian Humanitarian Conflict (Wii)
 * The six sounds, synthesised rather than shipped.
 *
 * A port of web/js/sfx.js. The browser builds every effect out of a few
 * oscillators and a noise burst instead of loading samples, and this does the
 * same thing for the same reasons: it keeps the game to one audio ASSET (the
 * track, which is 2.7MB and lives on the website's jukebox), and it lets the
 * sounds be tuned by editing numbers rather than re-exporting files.
 *
 * The difference is only when. WebAudio synthesises per play; this renders each
 * effect once into PCM at startup and hands the buffers to magnolia, because
 * ASND wants a buffer and there is no oscillator graph on a Wii. Six short
 * clips is about 100KB, against 6.5MB for the music -- the effects are the
 * cheap half by two orders of magnitude.
 *
 * ---------------------------------------------------------------------
 * This translation unit is ENGINE-FREE, and that is the point.
 *
 * No libogc, no GRRLIB, no magnolia -- just <math.h>. Synthesis is arithmetic,
 * so it can be checked on the host: that nothing clips, that the levels stay in
 * the order the design depends on, and that the ping's two notes land at the
 * spacing of the visual blink. None of those can be verified by listening to a
 * Wii through a television, which is the only other place this runs.
 *
 * Keep it that way, for the same reason sim.c stays engine-free.
 * ===================================================================== */
#ifndef SFX_H
#define SFX_H

/* The eight cues JovCue raises. AUDIO_MAX_SFX is 8, which is exactly enough --
 * a ninth sound needs an engine change, not just a slot here. */
typedef enum {
    SFX_SHOOT = 0,
    SFX_EXPLODE,
    SFX_PING,
    SFX_ESCORT,
    SFX_FRIENDLY_FIRE,
    SFX_LOST,
    SFX_LOCK,
    SFX_PLAYER_HIT,
    SFX_COUNT
} SfxId;

/* Sample rate the effects render at. Not the music's rate: these are short and
 * sharp and are what a player hears most precisely, so they get more than the
 * track does. Stated here rather than passed in because the test suite asserts
 * durations in milliseconds and needs to agree with what ships. */
#define SFX_RATE 32000

/* Longest effect, in samples, with headroom. friendlyFire is the longest at
 * 0.50s of tail; this is sized against SFX_RATE so a rate change cannot
 * silently truncate a sound. */
#define SFX_MAX_SAMPLES ((SFX_RATE * 3) / 4)

/* Total samples across all eight effects, with margin. Sized against what they
 * actually render -- 73,813 at SFX_RATE -- rather than SFX_COUNT * the longest,
 * which is 384KB for 144KB of sound. tests/test_sfx.c asserts the real total
 * fits, so a longer effect fails the suite instead of the console. */
#define SFX_POOL_SAMPLES 90000

/* Render one effect as signed 16-bit mono into `out`, and return the number of
 * samples written -- 0 if `id` is unknown or the buffer is too small.
 *
 * Deterministic: the noise bursts run off a seeded generator, so two renders of
 * the same effect are identical. That is not a musical property, it is a
 * testing one -- a suite that measured a fresh random burst every run would
 * have to assert loose enough bounds to catch nothing. */
int sfx_render(SfxId id, short *out, int max_samples);

/* Peak absolute sample of an effect, 0..32767. Exposed because "does it clip"
 * and "is it louder than that other one" are the two questions the levels are
 * designed around, and both are answerable without a speaker. */
int sfx_peak(SfxId id, short *scratch, int max_samples);

#endif
