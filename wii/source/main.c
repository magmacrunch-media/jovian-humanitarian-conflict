/* =====================================================================
 * main.c -- The Jovian Humanitarian Conflict (Wii)
 * The engine, the clock, the controller, and the three screens.
 *
 * Everything this file decides is about the CONSOLE. What a thing is worth,
 * what happens when a shot lands, and when a run is over all live in sim.c,
 * where `make test` can reach them -- see the note at the top of sim.h for why
 * the split falls there and not where the browser puts it.
 *
 * ---------------------------------------------------------------------
 * dt is in 60fps frames here, not seconds.
 *
 * The whole simulation is written in the browser's per-frame units, so this is
 * the one place the conversion happens:
 *
 *     dt = clock_dt() * 60
 *
 * 1.0 on NTSC, 1.2 on PAL, and the same game on both -- which
 * tests/test_simulation.c asserts directly rather than trusting. Capped at 2.0
 * so a hitch does not teleport the rail through a whole wave, the same cap
 * main.js applies for the same reason.
 * ===================================================================== */
#include <math.h>
#include <stdio.h>

#include <magnolia.h>

#include "assets.h"
#include "have_assets.h"
#include "render.h"
#include "sfx.h"
#include "sim.h"

/* -- The autopilot ----------------------------------------------------
 *
 * Compiled out entirely by default. Build with
 *
 *     make CFLAGS='-g -O2 -Wall $(MACHDEP) $(INCLUDE) -DAUTOPILOT=1'
 *
 * to skip the title, fly a run unattended, show the results for six seconds
 * and then stop driving.
 *
 * DO NOT spend an afternoon trying to drive Dolphin with SendInput instead.
 * Its emulated Wiimote reads the keyboard through DirectInput, which does not
 * observe injected keystrokes -- neither SendInput nor keybd_event reaches the
 * game, from a foreground or a background process, and NOTHING REPORTS AN
 * ERROR. The mouse is the exception, so a script can click A to start a run
 * and then find that no direction does anything, which reads exactly like a
 * broken input mapping. That was measured on makemecookies; see its
 * wii/AGENTS.md for the numbers.
 *
 * Both timeouts below matter. Without them the results card waits for an A
 * press that can never arrive, and main() starts a second run immediately --
 * which is how a capture ends up showing the NEXT run's empty scoreboard and
 * getting filed as this one's result.
 *
 * A clean autopilot run does not mean the game is tuned. The bot reads the
 * transponder off the struct, so it never mistakes a convoy for a hostile and
 * can tell you nothing about whether a person could. Only hands can.
 */
#ifndef AUTOPILOT
#define AUTOPILOT 0
#endif
#ifndef AUTOPILOT_EVERY
/* Frames between the bot's decisions -- its reaction time. At 1 it is
 * inhumanly sharp and the run says nothing about the tuning; 6 is about a
 * tenth of a second, which is fast but arguable. */
#define AUTOPILOT_EVERY 6
#endif
#define AUTOPILOT_CARD_FRAMES 360   /* six seconds on each end-of-run card */

/* -- Audio -------------------------------------------------------------
 *
 * The eight effects are synthesised into RAM at startup by sfx.c and handed to
 * magnolia as PCM; the music is one linked blob. Neither is required: a build
 * with no music.pcm plays no music, and an ASND that will not start plays
 * nothing at all. Both cases are silence, and silence is the one failure this
 * game can absorb without becoming unfair -- every identification channel the
 * premise rests on is visual, and the ping is a redundant fourth.
 *
 * That is worth stating because it is the opposite of makemecookies, where the
 * track is the shift clock and losing it loses the round.
 */
#if MUSIC_CHANNELS == 1
#define MUSIC_FMT AUDIO_MONO_16
#else
#define MUSIC_FMT AUDIO_STEREO_16
#endif

/* One pool for all eight clips rather than eight fixed buffers: the longest is
 * six times the shortest, and sizing every slot for friendlyFire would waste
 * most of it. */
/* -- Why this array is initialised, and must stay initialised ---------
 *
 * `= { 1 }` puts it in .data instead of .bss, and that is load-bearing.
 *
 * As a .bss array it broke the ENGINE'S GLYPH CACHE. Not audio -- text. Every
 * string in the game silently stopped drawing: the HUD, the results card, all
 * of it, while rectangles and the whole world drew perfectly. The cache
 * reported itself healthy throughout (34 entries, 37,518 hits, 20KB of
 * textures) because its bookkeeping was fine; the textures those entries
 * pointed at had been overwritten.
 *
 * Bisected to this: writing across a large .bss array corrupts memory the
 * allocator hands out LATER, so the glyph textures -- which are rasterised
 * lazily on the first frame, after audio has been set up -- came back empty.
 * Confining the writes to the first few thousand samples was fine; spreading
 * them across 74,000 was not; moving the identical array to .data fixed it
 * outright. So the .dol's .bss region and the heap overlap somewhere past its
 * start.
 *
 * The mechanism beneath that is NOT understood -- whether it is the loader,
 * libogc's arena calculation, or something about a .dol this size. What is
 * measured is the trigger and the fix.
 *
 * Two consequences worth keeping in mind:
 *   - Do not "tidy" the initialiser away. It costs 180KB in the .dol and buys
 *     text that draws.
 *   - Any OTHER large array added to this game should be initialised too, or
 *     checked. This one was found because it broke something loud and
 *     unrelated; a smaller one might just corrupt a score.
 */
static short sfx_pool[SFX_POOL_SAMPLES] = { 1 };
static int   sfx_ok;
static int   music_ok;
static unsigned int music_bytes;

static void audio_bring_up(void) {
    int i, at = 0;

    if (!audio_init()) { sfx_ok = 0; return; }
    sfx_ok = 1;

    for (i = 0; i < SFX_COUNT; i++) {
        short *buf = &sfx_pool[at];
        /* The cap is what is LEFT, not the per-effect maximum, so the pool
         * cannot be overrun however the effects are retuned. */
        int n = sfx_render((SfxId)i, buf, SFX_POOL_SAMPLES - at);
        if (n <= 0) continue;
        /* Bytes, not samples -- the loader takes a byte length, and passing
         * samples would play each clip for half its length and sound like a
         * truncation bug rather than an arithmetic one. */
        audio_load_sfx_mem_fmt(i, buf, (unsigned int)n * 2u,
                               AUDIO_MONO_16, SFX_RATE);
        at += n;
    }

    /* The web mix, carried across: CONFIG.MUSIC.VOLUME is 0.42 and the effects
     * play at full. The effects are the ones carrying information -- the ping
     * most of all -- so the track sits under them rather than beside them. */
    audio_set_music_volume((int)(MUSIC_LEVEL * 255.0f));
    audio_set_sfx_volume(255);

    /* HAVE_MUSIC comes from the Makefile, which knows whether audio/music.pcm
     * exists. It must NOT be `#ifdef music_pcm_size`: bin2s emits that as a
     * static const, not a macro, so the guard is always false and the music
     * silently never plays. That is exactly what happened, and it built and ran
     * without a word. */
#ifdef HAVE_MUSIC
    music_bytes = (unsigned int)music_pcm_size;
    if (music_bytes > 0) {
        music_ok = audio_play_music_mem_fmt(music_pcm, music_bytes,
                                            MUSIC_FMT, MUSIC_RATE);
    }
#endif

    /* One line, once, at startup -- not per-frame; an EXI write is not free.
     * It exists because the failure above was invisible: every other symptom
     * of "no music" is identical to "music is quiet". Now the log says which. */
    printf("audio: sfx=%d music=%d bytes=%u rate=%d ch=%d pool=%d/%d\n",
           sfx_ok, music_ok, music_bytes, MUSIC_RATE, MUSIC_CHANNELS,
           at, SFX_POOL_SAMPLES);
}

/* Turn a frame's cues into sound: each DISTINCT effect at most once, however
 * many times its event fired this frame.
 *
 * The first version played the counts -- two kills, two explosions -- on the
 * reasoning that collapsing them would make a busy frame quieter than a calm
 * one. It made the busy frames clip instead. Measured off Dolphin's audio dump:
 * 0.9% of the console's output at the rail, in runs up to 1.33ms, and every
 * burst of it landed on a frame the heartbeat showed raising several events at
 * once. The results card, which is music and nothing else, never clipped at
 * all -- which is what ruled the music out.
 *
 * Two identical impulses started on the same frame are not two sounds. They are
 * one sound 6dB louder, because they are sample-aligned; there is no extra
 * information in the second, only extra amplitude. Distinct effects still all
 * play, because those carry different information -- an escort and an explosion
 * in the same frame are two things the player needs to know.
 *
 * The browser gets away with playing the counts because WebAudio mixes into a
 * float destination with effectively unlimited headroom and its absolute levels
 * are tiny. ASND sums into 16 bits and clamps. */
static void play_cues(const JovCue *cue) {
    if (!sfx_ok) return;
    if (cue->shoot)         audio_play_sfx(SFX_SHOOT);
    if (cue->explode)       audio_play_sfx(SFX_EXPLODE);
    if (cue->ping)          audio_play_sfx(SFX_PING);
    if (cue->escort)        audio_play_sfx(SFX_ESCORT);
    if (cue->friendly_fire) audio_play_sfx(SFX_FRIENDLY_FIRE);
    if (cue->lost)          audio_play_sfx(SFX_LOST);
    if (cue->lock)          audio_play_sfx(SFX_LOCK);
    if (cue->player_hit)    audio_play_sfx(SFX_PLAYER_HIT);
}

/* The state machine is magnolia's, not ours.
 *
 * This file used to hand-roll three states. The engine ships the whole
 * score-attack shell -- title, ready, play, pause, game over, initials on a
 * qualifying score, then the table -- and its header makes the argument for
 * using it: the initials editor "is fiddly enough that every game copying it
 * would mean every game copying its bugs". It also owns the transitions, so
 * what is left here is when a run ENDS and what each card looks like.
 */
static GameStateMachine gs;

/* -- Attract mode -----------------------------------------------------
 *
 * What the cabinet does when nobody is playing: hold the title, play itself
 * for a while, show the best runs, and go round again. Any button drops out of
 * it, and A starts a real game through the ordinary shell path.
 *
 * The demo is flown by jov_bot() -- the same function AUTOPILOT uses, and the
 * same one the host suite flies to assert that restraint is achievable. That
 * is the reason it lives in sim.c rather than here: an attract demo is shipped
 * code, and this way it is shipped code that is tested.
 *
 * The bot is asked for a decision every ATTRACT_REACTION frames rather than
 * every frame. At one frame it plays perfectly and the demo looks like a
 * screensaver; at twelve it visibly hesitates, overshoots and corrects, which
 * is what makes it read as somebody playing. It is a demo, not a showcase --
 * a passer-by should think "I could do that", not "why would I bother".
 */
typedef enum { AT_TITLE = 0, AT_DEMO, AT_SCORES } AttractPhase;

/* Overridable from the build, because watching a full cycle at its real
 * cadence takes forty seconds and Dolphin's software renderer -- the only
 * backend that captures cleanly -- runs far below 60fps, which stretches that
 * into many minutes. Shorten them to see the whole thing:
 *
 *   make CFLAGS='... -DATTRACT_TITLE_FRAMES=120 -DATTRACT_DEMO_FRAMES=300'
 */
#ifndef ATTRACT_TITLE_FRAMES
#define ATTRACT_TITLE_FRAMES   (10 * 60)   /* before the demo starts */
#endif
#ifndef ATTRACT_DEMO_FRAMES
#define ATTRACT_DEMO_FRAMES    (22 * 60)   /* about as long as a real run */
#endif
#ifndef ATTRACT_SCORES_FRAMES
#define ATTRACT_SCORES_FRAMES  (8 * 60)
#endif
#ifndef ATTRACT_REACTION
#define ATTRACT_REACTION       12          /* frames between the bot's decisions */
#endif

/* Phase changes only -- a handful of lines per cycle, not per frame. An EXI
 * write is not free and this is the title screen, which is where a console
 * spends most of its life. */
#ifndef ATTRACT_TRACE
#define ATTRACT_TRACE 0
#endif
#if ATTRACT_TRACE
#define AT_LOG(...) printf(__VA_ARGS__)
#else
#define AT_LOG(...) ((void)0)
#endif

static AttractPhase attract_phase;
static float attract_frames;
static int   attract_running;   /* the demo is actually being flown */

static JovSim sim;
static JovRun run;

/* -- Input ------------------------------------------------------------
 * Held sideways: the D-pad flies, 1 or 2 fires. magnolia's INPUT_BTN_UP
 * already means visual up for that grip, so nothing is rotated here.
 *
 * The Nunchuk's analog stick would suit this game better than a D-pad and the
 * engine does not expose it yet. The simulation takes an axis in [-1, 1] and
 * does not care where it came from, so that is an engine change and not a
 * change here.
 */
static float axis_x(void) {
    float v = 0.0f;
    if (input_held(0, INPUT_BTN_LEFT))  v -= 1.0f;
    if (input_held(0, INPUT_BTN_RIGHT)) v += 1.0f;
    return v;
}

static float axis_y(void) {
    float v = 0.0f;
    if (input_held(0, INPUT_BTN_UP))   v -= 1.0f;
    if (input_held(0, INPUT_BTN_DOWN)) v += 1.0f;
    return v;
}

/* A is deliberately NOT a fire button. It is what magnolia's shell advances
 * every card with, so a run that begins on A would begin with A still held and
 * fire a shot the player never asked for -- on the first frame, at whatever
 * happens to be in front. The browser hit exactly this and says so in main.js:
 * "the Space that starts a run is still sitting in justPressed on frame 1".
 * Held sideways, 1 and 2 are under the thumb anyway and A is not. */
static int firing(void) {
    return input_held(0, INPUT_BTN_1) || input_held(0, INPUT_BTN_2);
}


/* Anything at all, so a passer-by touching the controller stops the demo.
 * Deliberately not just A: pressing anything is a statement of interest, and a
 * demo that carried on would be ignoring it. */
#if !AUTOPILOT
static int any_input(void) {
    const InputPad *p = input_snapshot(0);
    return p && p->pressed != 0;
}
#endif

/* -- Run lifecycle ----------------------------------------------------- */

static void start_run(unsigned int seed) {
    jov_sim_reset(&sim, seed);
    jov_run_reset(&run);
    rd_reset_stars(seed ^ 0x9E3779B9u);
}

/* Back to the title, demo abandoned. Called when somebody touches the
 * controller and whenever a real run begins. */
static void attract_reset(void) {
    attract_phase = AT_TITLE;
    attract_frames = 0.0f;
    if (attract_running) {
        /* The demo has been flying the real simulation, so the world is full
         * of its contacts. Put it back to a clean rail for the title. */
        attract_running = 0;
        start_run(1u);
    }
}

int main(void) {
    const MagnoliaConfig cfg = {
        "jovian",   /* -> sd:/apps/jovian/ */
        10,         /* max_scores */
        6           /* overscan_pct */
    };
    float card_frame = 0.0f;
    int status;
#if AUTOPILOT
    /* Only the autopilot counts frames on a card; a person presses A. Declared
       under the guard so the ordinary build does not carry a variable it sets
       and never reads. */
    int results_frames = 0;
#endif

    status = magnolia_init(&cfg);
    if (status == -2) return 1;   /* video never came up -- nothing is possible */

    input_init();
    rd_init();
    audio_bring_up();
    gamestate_init(&gs);

    /* The initials editor scrolls 26 letters, so it wants key repeat. Without
       it, entering "ZZZ" is seventy-eight presses. */
    input_set_repeat(24, 5);

    /* The rail drifts behind the title card, so it needs to exist before a run
     * does. Reset again on every start; this is only the backdrop's. */
    start_run(1u);

#if AUTOPILOT
    gamestate_set(&gs, GS_PLAYING);
    start_run((unsigned int)clock_frame() + 7u);
#endif

    while (1) {
        float dt;

        input_scan();
        if (input_home_pressed()) break;

        /* MINUS mutes everything, as M does in the browser. Not persisted:
         * magnolia has prefs, but a mute that survives a reboot is a mute
         * somebody sets by accident and then reports as broken audio. */
        if (input_minus_pressed()) audio_set_muted(!audio_get_muted());

        dt = clock_dt() * 60.0f;
        if (dt > 2.0f) dt = 2.0f;
        if (dt < 0.0f) dt = 0.0f;

        if (gamestate_current(&gs) == GS_PLAYING) {
            JovCue cue;
            float ax = axis_x(), ay = axis_y();
            int fire = firing();

#if AUTOPILOT
            if (((int)sim.frame) % AUTOPILOT_EVERY == 0) {
                static float hold_x, hold_y;
                static int hold_fire;
                jov_bot(&sim, &hold_x, &hold_y, &hold_fire);
                ax = hold_x; ay = hold_y; fire = hold_fire;
            } else {
                ax = 0.0f; ay = 0.0f; fire = 0;
            }
#else
            if (input_plus_pressed()) { gamestate_pause(&gs); }
#endif

            /* The gun is the one sound with no event behind it: firing is not
             * something that HAPPENED to the world, so entities.js never
             * reported it and neither does sim.c. main.js reads the same
             * `fired` return; this reads the shot count instead, which is the
             * same fact and needs no second call into the simulation. */
            {
                int shots_before = sim.n_shots;
                jov_sim_update(&sim, ax, ay, fire, dt);
                jov_run_resolve(&run, &sim, &cue);
                if (sim.n_shots > shots_before) cue.shoot++;
            }
            jov_run_tick(&run, dt);
            play_cues(&cue);

            if (cue.shake > 0.0f) rd_add_shake(cue.shake);
            if (cue.flash > 0.0f) rd_add_flash(cue.flash, cue.flash_color);
            rd_decay(dt);

#if AUTOPILOT
            /* A per-second heartbeat, so a run that never reaches the results
             * card can be told apart from one that never started. */
            {
                static int last_sec = -1;
                int sec = (int)(sim.frame / 60.0f);
                if (sec != last_sec) {
                    last_sec = sec;
                    printf("t=%03ds dt=%.2f score=%d contacts=%d shots=%d "
                           "lives=%d strikes=%d\n",
                           sec, dt, run.score, sim.n_contacts, sim.n_shots,
                           sim.player.lives, run.strikes);
                }
            }
#endif

            if (jov_run_over(&run, &sim.player)) {
                /* The shell works out whether it qualifies and where. */
                gamestate_end_run(&gs, run.score);
#if AUTOPILOT
                results_frames = 0;
                printf("run: score=%d kills=%d escorted=%d lost=%d strikes=%d "
                       "lives=%d rank=%s frames=%.0f dist=%.0f\n",
                       run.score, run.kills, run.escorted, run.lost,
                       run.strikes, sim.player.lives, jov_run_rank(&run),
                       sim.frame, sim.rail.distance);
                printf("caps: events_dropped=%d waves_dropped=%d "
                       "waves_spawned=%d\n",
                       sim.events_dropped, sim.waves_dropped, sim.waves_spawned);
                printf("score: high=%d rank=%d persisted=%d entries=%d\n",
                       gs.is_high_score, gs.rank, scoring_persisted(),
                       scoring_get_count());
                {
                    int te, th, tm, tv;
                    unsigned long tb;
                    text_stats(&te, &th, &tm, &tv, &tb);
                    printf("text: enabled=%d entries=%d hits=%d misses=%d "
                           "evictions=%d bytes=%lu\n",
                           text_cache_enabled(), te, th, tm, tv, tb);
                }
#endif
            }
        } else if (gamestate_current(&gs) == GS_PAUSED) {
            if (input_plus_pressed()) gamestate_resume(&gs);
        } else {
            card_frame += dt;

            /* The attract cycle runs only on the title -- every other card is
             * something a player is looking at. */
            if (gamestate_current(&gs) == GS_TITLE) {
#if !AUTOPILOT
                if (any_input()) attract_reset();
                attract_frames += dt;

                switch (attract_phase) {
                case AT_TITLE:
                    if (attract_frames > ATTRACT_TITLE_FRAMES) {
                        AT_LOG("attract: demo\n");
                        attract_phase = AT_DEMO;
                        attract_frames = 0.0f;
                        attract_running = 1;
                        /* Seeded from the clock, so the demo is a different
                         * run each time round rather than the same scripted
                         * one -- the whole point of having a simulation to
                         * fly is that the attract screen never repeats. */
                        start_run((unsigned int)clock_frame() * 2654435761u + 9u);
                    }
                    break;

                case AT_DEMO: {
                    JovCue cue;
                    static float hold_x, hold_y;
                    static int hold_fire;
                    float ax, ay;
                    int fire;

                    if (((int)sim.frame) % ATTRACT_REACTION == 0) {
                        jov_bot(&sim, &hold_x, &hold_y, &hold_fire);
                    }
                    ax = hold_x; ay = hold_y; fire = hold_fire;

                    {
                        int shots_before = sim.n_shots;
                        jov_sim_update(&sim, ax, ay, fire, dt);
                        jov_run_resolve(&run, &sim, &cue);
                        if (sim.n_shots > shots_before) cue.shoot++;
                    }
                    jov_run_tick(&run, dt);
                    play_cues(&cue);
                    if (cue.shake > 0.0f) rd_add_shake(cue.shake);
                    if (cue.flash > 0.0f) rd_add_flash(cue.flash, cue.flash_color);
                    rd_decay(dt);

                    /* Move on when the demo has run long enough OR the bot has
                     * died -- whichever first. A demo that sat on a dead ship
                     * would be advertising the wrong thing. */
                    if (attract_frames > ATTRACT_DEMO_FRAMES
                        || jov_run_over(&run, &sim.player)) {
                        AT_LOG("attract: scores (demo scored %d)\n",
                               run.score);
                        attract_phase = AT_SCORES;
                        attract_frames = 0.0f;
                        attract_running = 0;
                    }
                    break;
                }

                case AT_SCORES:
                    if (attract_frames > ATTRACT_SCORES_FRAMES) {
                        AT_LOG("attract: title\n");
                        attract_reset();
                    }
                    break;
                }
#endif
                if (!attract_running) jov_rail_drift(&sim.rail, card_frame);
            }
            /* Returns 1 on the frame it enters GS_PLAYING, which is the one
             * moment a fresh world is wanted. Seeded from the frame the player
             * pressed A -- the one genuinely unpredictable number a console
             * offers at boot. */
            if (gamestate_update(&gs, run.score)) {
                attract_reset();
                start_run((unsigned int)clock_frame() * 2654435761u + 1u);
            }
#if AUTOPILOT
            /* Walk the end-of-run cards, since nobody is going to press A.
             *
             * The shell advances on input_a_pressed() and the autopilot has no
             * way to synthesise one, so this drives the shell directly instead.
             * That is the same job the hook already does for flight -- and it
             * means the initials editor and the table are exercised by an
             * unattended run rather than only by hand, which matters because
             * they are the two screens a player reaches exactly once and only
             * after doing well.
             *
             * Each card gets AUTOPILOT_CARD_FRAMES so a capture has time to
             * find it. The letters are left at the default AAA: driving the
             * editor's cursor as well would be testing magnolia's code, which
             * has its own tests.
             */
            results_frames++;
            if (results_frames > AUTOPILOT_CARD_FRAMES) {
                results_frames = 0;
                switch (gamestate_current(&gs)) {
                case GS_GAME_OVER:
                    if (gs.is_high_score) {
                        gamestate_begin_initials(&gs);
                        printf("autopilot: entering initials at rank %d\n", gs.rank);
                    } else {
                        printf("autopilot: no high score, done\n");
                        magnolia_shutdown();
                        return 0;
                    }
                    break;
                case GS_INITIALS:
                    gamestate_commit_initials(&gs, run.score);
                    printf("autopilot: committed '%s', table now %d entries, "
                           "persisted=%d\n",
                           gs.initials, scoring_get_count(), scoring_persisted());
                    break;
                case GS_HIGH_SCORES:
                default:
                    printf("autopilot: done, shutting down\n");
                    magnolia_shutdown();
                    return 0;
                }
            }
#endif
        }

        renderer_draw_background();

        /* The playfield is scissored to its letterbox and the HUD is not:
         * world geometry is wider than the frame by design -- a deck band is
         * 1800 world units across -- and without the clip it runs out over the
         * letterbox and into the overscan. The HUD is authored against the
         * whole safe area and must not be cut, so the pair is explicit here
         * rather than hidden inside the draw calls. */
        rd_playfield_begin();
        rd_draw_rail(&sim.rail);
        /* Contacts and the ship whenever there is a run to show -- which
         * includes the attract demo, where the whole point is that the title
         * screen is playing the actual game. */
        if (gamestate_current(&gs) != GS_TITLE || attract_running) {
            rd_draw_contacts(&sim);
            rd_draw_player(&sim);
        }
        rd_playfield_end();

        switch (gamestate_current(&gs)) {
        case GS_TITLE:
            /* The title state has three faces while the cabinet attracts. */
            if (attract_phase == AT_DEMO) {
                rd_draw_hud(&sim, &run);
                rd_draw_demo_banner(card_frame);
            } else if (attract_phase == AT_SCORES) {
                rd_draw_scores(&gs);
            } else {
                rd_draw_title_text();
            }
            break;
        case GS_READY:
            rd_draw_hud(&sim, &run);
            rd_draw_ready(card_frame);
            break;
        case GS_PLAYING:
            rd_draw_hud(&sim, &run);
            break;
        case GS_PAUSED:
            rd_draw_hud(&sim, &run);
            rd_draw_paused();
            break;
        case GS_GAME_OVER:
            rd_draw_hud(&sim, &run);
            rd_draw_results(&run);
            break;
        case GS_INITIALS:
            rd_draw_initials(&gs, &run);
            break;
        case GS_HIGH_SCORES:
            rd_draw_scores(&gs);
            break;
        default:
            break;
        }

        renderer_finish();
    }

    magnolia_shutdown();
    return 0;
}
