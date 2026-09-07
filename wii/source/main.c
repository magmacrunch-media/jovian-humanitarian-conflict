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

#include "render.h"
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
#define AUTOPILOT_RESULTS_FRAMES 360   /* six seconds on the results card */

typedef enum { ST_TITLE = 0, ST_PLAYING, ST_RESULTS } State;

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

static int firing(void) {
    return input_held(0, INPUT_BTN_1) || input_held(0, INPUT_BTN_2)
        || input_a_held();
}

#if AUTOPILOT
/*
 * Steer at the nearest hostile inside firing range and shoot it, and hold fire
 * whenever a convoy is in the way. Deliberately a perfect prioritiser: what it
 * is for is proving the rules run for a whole flight on real hardware, not
 * proving they are fun.
 */
static void autopilot(float *ax, float *ay, int *fire) {
    const JovContact *target = 0;
    float best = 1e30f;
    int i;

    *ax = 0.0f; *ay = 0.0f; *fire = 0;

    for (i = 0; i < sim.n_contacts; i++) {
        const JovContact *c = &sim.contacts[i];
        if (c->kind != JOV_HOSTILE || c->dead) continue;
        if (c->z > Z_FIRE_MAX || c->z < 0.0f) continue;
        if (c->z < best) { best = c->z; target = c; }
    }
    if (!target) return;

    if (target->x > sim.player.x + 1.0f) *ax = 1.0f;
    else if (target->x < sim.player.x - 1.0f) *ax = -1.0f;
    if (target->y > sim.player.y + 1.0f) *ay = 1.0f;
    else if (target->y < sim.player.y - 1.0f) *ay = -1.0f;

    {
        float hw, hh, mx, my;
        jov_player_muzzle(&sim.player, &mx, &my);
        jov_hit_box(target, &hw, &hh);
        if (!proj_in_box(mx, my, target->x, target->y, hw, hh)) return;

        /* Hold fire if any convoy nearer than the target is also in the line.
         * This is the bot obeying the same rule the player is asked to. */
        for (i = 0; i < sim.n_contacts; i++) {
            const JovContact *c = &sim.contacts[i];
            float aw, ah;
            if (c->kind != JOV_AID || c->dead || c->z > target->z) continue;
            jov_hit_box(c, &aw, &ah);
            if (proj_in_box(mx, my, c->x, c->y, aw, ah)) return;
        }
        *fire = 1;
    }
}
#endif

/* -- Run lifecycle ----------------------------------------------------- */

static void start_run(unsigned int seed) {
    jov_sim_reset(&sim, seed);
    jov_run_reset(&run);
    rd_reset_stars(seed ^ 0x9E3779B9u);
}

int main(void) {
    const MagnoliaConfig cfg = {
        "jovian",   /* -> sd:/apps/jovian/ */
        10,         /* max_scores */
        6           /* overscan_pct */
    };
    State state = ST_TITLE;
    float title_frame = 0.0f;
    int results_frames = 0;
    int status;

    status = magnolia_init(&cfg);
    if (status == -2) return 1;   /* video never came up -- nothing is possible */

    input_init();
    rd_init();

    /* The rail drifts behind the title card, so it needs to exist before a run
     * does. Reset again on every start; this is only the backdrop's. */
    start_run(1u);

#if AUTOPILOT
    state = ST_PLAYING;
    start_run((unsigned int)clock_frame() + 7u);
#endif

    while (1) {
        float dt;

        input_scan();
        if (input_home_pressed()) break;

        dt = clock_dt() * 60.0f;
        if (dt > 2.0f) dt = 2.0f;
        if (dt < 0.0f) dt = 0.0f;

        switch (state) {
        case ST_TITLE:
            title_frame += dt;
            jov_rail_drift(&sim.rail, title_frame);
            if (input_a_pressed() || input_button2_pressed()
                || input_button1_pressed()) {
                /* Seeded from the frame the player pressed A, which is the one
                 * genuinely unpredictable number a console offers at boot. */
                start_run((unsigned int)clock_frame() * 2654435761u + 1u);
                state = ST_PLAYING;
            }
            break;

        case ST_PLAYING: {
            JovCue cue;
            float ax = axis_x(), ay = axis_y();
            int fire = firing();

#if AUTOPILOT
            if (((int)sim.frame) % AUTOPILOT_EVERY == 0) {
                static float hold_x, hold_y;
                static int hold_fire;
                autopilot(&hold_x, &hold_y, &hold_fire);
                ax = hold_x; ay = hold_y; fire = hold_fire;
            } else {
                ax = 0.0f; ay = 0.0f; fire = 0;
            }
#endif

            jov_sim_update(&sim, ax, ay, fire, dt);
            jov_run_resolve(&run, &sim, &cue);
            jov_run_tick(&run, dt);

#if AUTOPILOT
            /* A heartbeat, so a run that never reaches the results card can be
             * told apart from one that never started. Without it, "the log is
             * empty" has two very different causes and no way to choose. */
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

            /* Cues are collected and, for now, only shaken with: this game
             * ships no audio at all. The web version's six sound effects are
             * synthesised in WebAudio and its music lives on the website's
             * jukebox, so there was nothing to convert -- see ../AGENTS.md.
             * The counts are here so that wiring audio later is a change in
             * this block and nowhere else. */
            if (cue.shake > 0.0f) rd_add_shake(cue.shake);
            if (cue.flash > 0.0f) rd_add_flash(cue.flash, cue.flash_color);
            rd_decay(dt);

            if (jov_run_over(&run, &sim.player)) {
                state = ST_RESULTS;
                results_frames = 0;
            }
            break;
        }

        case ST_RESULTS:
            results_frames++;
#if AUTOPILOT
            /* The run, in the log. A screenshot of a results card can be a
             * screenshot of the WRONG results card -- the next run's, or
             * whatever Dolphin had in front when the capture fired -- and
             * neither failure announces itself. A trace cannot be mistaken for
             * a different run, so this is what an unattended run is read from
             * and the screenshot is only corroboration.
             *
             * Reaches Dolphin's log through SYS_STDIO_Report(true), which
             * magnolia_init() calls -- and needs OSREPORT and WriteToFile in
             * Dolphin's Logger.ini, both of which default to False. */
            if (results_frames == 1) {
                printf("run: score=%d kills=%d escorted=%d lost=%d strikes=%d "
                       "lives=%d rank=%s frames=%.0f dist=%.0f\n",
                       run.score, run.kills, run.escorted, run.lost,
                       run.strikes, sim.player.lives, jov_run_rank(&run),
                       sim.frame, sim.rail.distance);
                printf("caps: events_dropped=%d waves_dropped=%d "
                       "waves_spawned=%d\n",
                       sim.events_dropped, sim.waves_dropped, sim.waves_spawned);
            }

            /* Stop driving rather than starting a second run. Note that
             * returning from main() does NOT close Dolphin, so a scripted
             * capture still has to close the emulator itself. */
            if (results_frames > AUTOPILOT_RESULTS_FRAMES) {
                printf("autopilot: done, shutting down\n");
                magnolia_shutdown();
                return 0;
            }
#endif
            if (results_frames > 30 && (input_a_pressed()
                || input_button2_pressed() || input_button1_pressed())) {
                start_run((unsigned int)clock_frame() * 2654435761u + 1u);
                state = ST_PLAYING;
            }
            break;
        }

        renderer_draw_background();

        /* The playfield is scissored to its letterbox and the HUD is not:
         * world geometry is wider than the frame by design -- a deck band is
         * 1800 world units across -- and without the clip it runs out over the
         * letterbox and into the overscan. The HUD is authored against the
         * whole safe area and must not be cut, so the pair is explicit here
         * rather than hidden inside the draw calls. */
        if (state == ST_TITLE) {
            rd_playfield_begin();
            rd_draw_rail(&sim.rail);
            rd_playfield_end();
            rd_draw_title_text();
        } else {
            rd_playfield_begin();
            rd_draw_rail(&sim.rail);
            rd_draw_contacts(&sim);
            rd_draw_player(&sim);
            rd_playfield_end();

            rd_draw_hud(&sim, &run);
            if (state == ST_RESULTS) rd_draw_results(&run);
        }

        renderer_finish();
    }

    magnolia_shutdown();
    return 0;
}
