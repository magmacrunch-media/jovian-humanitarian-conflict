/* =====================================================================
 * test_simulation.c -- The Jovian Humanitarian Conflict (Wii) headless tests.
 *
 * source/sim.c and source/projection.c are the whole simulation and touch no
 * libogc, no GRRLIB and no drawing -- that separation is deliberate, so the
 * rules can be exercised without a console or an emulator. This links the real
 * shipped translation units. Nothing here is a reimplementation that could
 * drift from what ships.
 *
 * The checks are ported from web/tests/test-simulation.js, case for case, and
 * that is the point of them: the browser version is the source of truth for
 * rules and tuning, and two versions of one game are only one game for as long
 * as something keeps checking. What this guards, in order of how much it would
 * hurt:
 *
 *  1. The fairness invariant. The whole premise is that refusing to shoot is a
 *     decision rather than a gamble, which is only true if a convoy squawks its
 *     transponder long enough to be identified BEFORE it can be shot. That is a
 *     relationship between four constants that a plausible-looking tuning pass
 *     can silently break -- raise RAIL_SPEED_MAX and the game is still fun and
 *     now unfair. Asserted on the constants and again by simulation.
 *
 *  2. Friendly-fire attribution. A convoy killed by a hostile costs a combo;
 *     one killed by the player costs 1,000 points and a third of the run.
 *     Swapping those is the cruellest bug this game could have, because it
 *     punishes the player for the thing they did right.
 *
 *  3. Frame-rate independence. Every quantity is scaled by dt. Three step sizes
 *     covering the same wall-clock time must agree -- which matters more here
 *     than in the browser, because a Wii frame is 1/60 on NTSC and 1/50 on PAL
 *     and the same run has to play the same on both. There is a check for
 *     exactly that pair, which the web suite has no reason to carry.
 *
 *  4. Projection sanity. Draw and collision both go through it, so a transform
 *     that is not monotonic in z would produce shots that visibly connect and
 *     do nothing.
 *
 *  5. Capacity. The browser grows its arrays; this cannot. A dropped event is
 *     an unpaid score and a dropped wave is a hole in the run, so both counters
 *     are asserted to stay at zero across a long flight. This has no web
 *     counterpart -- it is the cost of the port and therefore its risk.
 *
 * Two tolerances are looser here than in the browser, and only two. The
 * simulation runs in float rather than double, so the projection round-trip is
 * checked to 0.01 world units instead of 1e-6, and the rest of the numeric
 * comparisons carry the web suite's tolerances unchanged. Where a tolerance was
 * relaxed the line says so.
 *
 * Run: make test
 * ===================================================================== */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sim.h"

/* -- Harness ---------------------------------------------------------
 * The same shape as magnolia's tests/harness.h, reproduced rather than
 * included because the host build links this game's sources alone -- CI runs
 * `make test` from a single checkout with no engine beside it.
 */

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

static void check_detail(int cond, const char *what, const char *fmt, double a, double b) {
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

static void check_near(double a, double b, double tol, const char *what) {
    check_detail(fabs(a - b) <= tol, what, "got %g, expected %g", a, b);
}

static void section(const char *title) {
    printf("\n%s\n", title);
}

/* -- Fixtures ---------------------------------------------------------
 * The web suite pushes literal objects onto entities.contacts. The struct is
 * public here for the same reason, so this is the same manoeuvre.
 */
static JovContact *push_contact(JovSim *s, JovKind kind, float x, float y, float z) {
    JovContact *c = &s->contacts[s->n_contacts++];
    memset(c, 0, sizeof(*c));
    c->id = s->next_id++;
    c->kind = kind;
    c->x = x; c->y = y; c->z = z;
    c->drift_seed = 1.0f;
    return c;
}

static void push_shot(JovSim *s, float x, float y, float z) {
    s->shots[s->n_shots].x = x;
    s->shots[s->n_shots].y = y;
    s->shots[s->n_shots].z = z;
    s->n_shots++;
}

static int has_event(const JovSim *s, JovEventType t) {
    int i;
    for (i = 0; i < s->n_events; i++) if (s->events[i].type == t) return 1;
    return 0;
}

static int count_event(const JovSim *s, JovEventType t) {
    int i, n = 0;
    for (i = 0; i < s->n_events; i++) if (s->events[i].type == t) n++;
    return n;
}

int main(void) {
    printf("The Jovian Humanitarian Conflict -- Wii simulation tests\n");

    /* =================================================================
     * 1. The fairness invariant
     * ================================================================= */
    section("fairness -- a convoy is identifiable before it can be shot");
    {
        /* Two full squawk cycles is what "identifiable" means here, and the
         * double blink has to fit inside one period for that to be true. */
        check(BLINK_ON_2 < BLINK_PERIOD && BLINK_GAP < BLINK_ON_2,
              "the double blink fits inside one squawk period");

        check(TELEGRAPH_MIN_FRAMES >= (float)BLINK_PERIOD * 2.0f,
              "the telegraph budget covers at least two squawk cycles");

        /* The invariant proper, at the WORST case: full difficulty, top rail
         * speed. */
        {
            float budget = TELEGRAPH_MIN_FRAMES + REACTION_FRAMES;
            float available = jov_diff_identify_frames(1.0f);
            check_detail(available >= budget,
                         "identification window survives the fastest rail",
                         "have %.1f frames, need %.0f", available, budget);

            /* And at the easiest, which must not somehow be worse. */
            check(jov_diff_identify_frames(0.0f) >= jov_diff_identify_frames(1.0f),
                  "the window only ever narrows with difficulty");

            /* Simulated, not just arithmetic: fly a convoy from spawn and count
             * the frames it squawks before it enters firing range. */
            {
                JovSim s;
                JovContact *c;
                int frames_before_firable = 0, lit_frames = 0, f = 0;
                float rail_max = jov_diff_rail_speed(1.0f);

                jov_sim_reset(&s, 4);
                c = push_contact(&s, JOV_AID, 0.0f, 0.0f, Z_FAR);
                while (c->z > Z_FIRE_MAX) {
                    if (jov_beacon_lit(c, (float)f)) lit_frames++;
                    c->z -= rail_max;
                    frames_before_firable++;
                    f++;
                }
                check_detail(frames_before_firable >= budget,
                             "a spawned convoy is out of range for the whole telegraph budget",
                             "%.0f frames vs %.0f", (double)frames_before_firable, budget);
                check_detail(lit_frames >= 10,
                             "and its transponder is actually lit during that window",
                             "lit on %.0f of %.0f frames",
                             (double)lit_frames, (double)frames_before_firable);
            }
        }

        /* The absence is the signal. A hostile that squawked would not merely
         * be wrong, it would invert the one channel the whole premise rests
         * on -- and the ping check below covers the SOUND, which is a
         * different code path entirely. Both halves, or neither is guarded. */
        {
            JovContact aid, hostile;
            int f, aid_lit = 0, hostile_lit = 0;
            memset(&aid, 0, sizeof(aid)); aid.kind = JOV_AID;
            memset(&hostile, 0, sizeof(hostile)); hostile.kind = JOV_HOSTILE;
            for (f = 0; f < BLINK_PERIOD * 4; f++) {
                if (jov_beacon_lit(&aid, (float)f)) aid_lit++;
                if (jov_beacon_lit(&hostile, (float)f)) hostile_lit++;
            }
            check_detail(hostile_lit == 0, "a hostile is dark on every frame",
                         "lit on %.0f of %.0f", (double)hostile_lit,
                         (double)(BLINK_PERIOD * 4));
            check_detail(aid_lit == (BLINK_ON_1 + (BLINK_ON_2 - BLINK_GAP)) * 4,
                         "and a convoy squawks exactly the double-tap, four cycles running",
                         "lit on %.0f frames, expected %.0f", (double)aid_lit,
                         (double)((BLINK_ON_1 + (BLINK_ON_2 - BLINK_GAP)) * 4));
        }

        /* Shape becomes a usable second channel while still out of range. */
        check(Z_SHAPE_READABLE < Z_FAR && Z_SHAPE_READABLE > 0.0f,
              "silhouette threshold is inside the rail");
        check_detail(proj_scale_at(Z_SHAPE_READABLE) >= 0.3f,
                     "the silhouette threshold is a size a shape can actually be read at",
                     "scale %.3f, need %.1f", proj_scale_at(Z_SHAPE_READABLE), 0.3);
    }

    /* =================================================================
     * 1b. The audible transponder
     * ================================================================= */
    section("the ping -- the transponder heard, not seen");
    {
        /* It has to sound while the player still has every option, which means
         * outside firing range with the whole reaction budget left. */
        check(Z_PING > Z_FIRE_MAX, "the ping sounds before a convoy can be shot");
        {
            float after = (Z_PING - Z_FIRE_MAX) / jov_diff_rail_speed(1.0f);
            check_detail(after >= REACTION_FRAMES,
                         "and leaves at least a reaction to act on it",
                         "%.1f frames, need %.0f", after, REACTION_FRAMES);
        }

        /* Exactly once per convoy. Firing every frame inside Z_PING would turn
         * an identification cue into a drone nobody hears. */
        {
            JovSim s;
            int i, pings = 0;
            float first_ping_z = -1.0f;

            jov_sim_reset(&s, 8);
            push_contact(&s, JOV_AID, 0.0f, 0.0f, Z_FAR);
            for (i = 0; i < 400; i++) {
                int j;
                jov_sim_update_contacts(&s, 4.0f, 1.0f);
                for (j = 0; j < s.n_events; j++) {
                    if (s.events[j].type == JOV_EV_AID_SIGHTED) {
                        pings++;
                        if (first_ping_z < 0.0f) first_ping_z = s.events[j].z;
                    }
                }
                s.n_events = 0;
            }
            check_detail(pings == 1, "a convoy pings exactly once",
                         "pinged %.0f times", (double)pings, 1.0);
            check_detail(first_ping_z > Z_FIRE_MAX,
                         "and it pings while still out of range",
                         "first ping at z=%.0f, range ends at %.0f",
                         first_ping_z, Z_FIRE_MAX);
        }

        /* Hostiles are silent, the same way they are dark. */
        {
            JovSim s;
            int i, hostile_pings = 0;
            jov_sim_reset(&s, 9);
            push_contact(&s, JOV_HOSTILE, 0.0f, 0.0f, Z_FAR);
            for (i = 0; i < 400; i++) {
                jov_sim_update_contacts(&s, 4.0f, 1.0f);
                hostile_pings += count_event(&s, JOV_EV_AID_SIGHTED);
                s.n_events = 0;
            }
            check_detail(hostile_pings == 0, "a hostile never pings",
                         "pinged %.0f times", (double)hostile_pings, 0.0);
        }

        /* Every convoy in a real run gets announced; none silently slips
         * through. */
        {
            JovSim s;
            int f, spawned_aid = 0, sighted = 0, highest_id = 0;
            jov_sim_reset(&s, 10);
            for (f = 0; f < 3000; f++) {
                int i;
                jov_sim_update(&s, 0.0f, 0.0f, 0, 1.0f);
                sighted += count_event(&s, JOV_EV_AID_SIGHTED);
                s.n_events = 0;
                /* ids are handed out in ascending order, so anything above the
                 * high-water mark is new this frame. */
                for (i = 0; i < s.n_contacts; i++) {
                    if (s.contacts[i].id > highest_id) {
                        highest_id = s.contacts[i].id;
                        if (s.contacts[i].kind == JOV_AID) spawned_aid++;
                    }
                }
            }
            check_detail(spawned_aid > 10,
                         "the run actually produced convoys to check",
                         "%.0f convoys, need more than %.0f", (double)spawned_aid, 10.0);
            check_detail(sighted >= spawned_aid - 2,
                         "essentially every convoy announced itself",
                         "%.0f pings for %.0f convoys", (double)sighted, (double)spawned_aid);
        }
    }

    /* =================================================================
     * 2. Friendly-fire attribution
     * ================================================================= */
    section("attribution -- who killed the convoy");
    {
        /* Shot by the player. */
        {
            JovSim s;
            jov_sim_reset(&s, 2);
            push_contact(&s, JOV_AID, 0.0f, 0.0f, 200.0f);
            push_shot(&s, 0.0f, 0.0f, 190.0f);
            jov_sim_update_shots(&s, 1.0f);
            check(s.n_events == 1 && s.events[0].type == JOV_EV_FRIENDLY_FIRE,
                  "a convoy hit by the player raises friendly-fire");
        }

        /* Killed by a hostile lock instead. */
        {
            JovSim s;
            JovContact *aid;
            jov_sim_reset(&s, 3);
            aid = push_contact(&s, JOV_AID, 0.0f, 0.0f, 300.0f);
            aid->doom_timer = 1.0f;
            aid->locked_by = 2;
            push_contact(&s, JOV_HOSTILE, 10.0f, 0.0f, 300.0f)->aggro = 1;
            jov_sim_update_contacts(&s, 0.0f, 1.0f);
            check(has_event(&s, JOV_EV_AID_LOST) && !has_event(&s, JOV_EV_FRIENDLY_FIRE),
                  "a convoy killed by a hostile raises aid-lost, never friendly-fire");
        }

        /* Killing the attacker calls the strike off -- the rescue has to
         * actually work. */
        {
            JovSim s;
            JovContact *aid;
            int i, found = 0;
            jov_sim_reset(&s, 5);
            aid = push_contact(&s, JOV_AID, 0.0f, 0.0f, 300.0f);
            aid->doom_timer = 40.0f;
            aid->locked_by = 2;
            push_contact(&s, JOV_HOSTILE, 0.0f, 0.0f, 260.0f);
            push_shot(&s, 0.0f, 0.0f, 252.0f);
            jov_sim_update_shots(&s, 1.0f);
            s.n_events = 0;
            jov_sim_update_contacts(&s, 0.0f, 1.0f);
            for (i = 0; i < s.n_contacts; i++) {
                if (s.contacts[i].kind == JOV_AID) {
                    found = 1;
                    check_detail(s.contacts[i].doom_timer == 0.0f,
                                 "killing the attacker releases the convoy it had locked",
                                 "doom_timer=%g, expected %g",
                                 s.contacts[i].doom_timer, 0.0);
                }
            }
            check(found, "and the convoy is still on the rail to be released");
        }

        /* A convoy that reaches the camera alive is an escort, not a loss. */
        {
            JovSim s;
            jov_sim_reset(&s, 6);
            push_contact(&s, JOV_AID, 0.0f, 0.0f, Z_NEAR + 1.0f);
            jov_sim_update_contacts(&s, 10.0f, 1.0f);
            check(has_event(&s, JOV_EV_AID_ESCORTED),
                  "a convoy that gets past the camera is escorted");
        }

        /* And the price list agrees with the attribution. Pricing lives in
         * sim.c here rather than main.c precisely so this can be asserted --
         * see the note at the top of sim.h. */
        {
            JovSim s; JovRun run; JovCue cue;
            jov_sim_reset(&s, 66);
            jov_run_reset(&run);
            push_contact(&s, JOV_AID, 0.0f, 0.0f, 200.0f);
            push_shot(&s, 0.0f, 0.0f, 190.0f);
            jov_sim_update_shots(&s, 1.0f);
            jov_run_resolve(&run, &s, &cue);
            check_detail(run.score == SCORE_FRIENDLY_FIRE,
                         "shooting a convoy costs the full penalty",
                         "score %.0f, expected %.0f",
                         (double)run.score, (double)SCORE_FRIENDLY_FIRE);
            check(run.strikes == 1, "and one of three strikes");
            check(run.combo == 1, "and the combo with it");
        }

        {
            JovSim s; JovRun run; JovCue cue;
            jov_sim_reset(&s, 67);
            jov_run_reset(&run);
            push_contact(&s, JOV_HOSTILE, 0.0f, 0.0f, 200.0f);
            push_shot(&s, 0.0f, 0.0f, 190.0f);
            jov_sim_update_shots(&s, 1.0f);
            jov_run_resolve(&run, &s, &cue);
            check_detail(run.score == SCORE_HOSTILE,
                         "a kill pays at the combo it was made on",
                         "score %.0f, expected %.0f",
                         (double)run.score, (double)SCORE_HOSTILE);
            check(run.strikes == 0 && run.combo == 2,
                  "costs no strike and advances the streak");
        }

        {
            /* An escort is the other half of the price list, and the half a
             * bot never exercises: it is paid when a convoy leaves the frame
             * alive, which only happens when the player did nothing. Left
             * unchecked, "escort pays nothing" is a mutation that survives a
             * whole suite -- it did survive this one, which is why the check
             * is here. */
            JovSim s; JovRun run; JovCue cue;
            jov_sim_reset(&s, 68);
            jov_run_reset(&run);
            push_contact(&s, JOV_AID, 0.0f, 0.0f, Z_NEAR + 1.0f);
            jov_sim_update_contacts(&s, 10.0f, 1.0f);
            jov_run_resolve(&run, &s, &cue);
            check_detail(run.score == SCORE_ESCORT,
                         "a convoy escorted clear pays the escort bonus",
                         "score %.0f, expected %.0f",
                         (double)run.score, (double)SCORE_ESCORT);
            check(run.escorted == 1 && run.combo == 2,
                  "counts toward the record and advances the streak");
            check_detail(SCORE_ESCORT > SCORE_HOSTILE * COMBO_MAX / 2,
                         "and is worth more than the shooting it competes with",
                         "escort %.0f vs a mid-combo kill %.0f",
                         (double)SCORE_ESCORT, (double)(SCORE_HOSTILE * COMBO_MAX / 2));
        }

        {
            /* Three strikes ends the run whatever the score says. */
            JovRun run; JovPlayer p;
            jov_run_reset(&run);
            jov_player_reset(&p);
            run.score = 999999;
            run.strikes = MAX_STRIKES;
            check(jov_run_over(&run, &p), "three strikes ends the run outright");
            check(strcmp(jov_run_rank(&run), "COURT-MARTIALLED") == 0,
                  "and the rank says why");
        }
    }

    /* =================================================================
     * 3. dt invariance
     * ================================================================= */
    section("dt invariance -- the same wall-clock time, three step sizes");
    {
        float ax[3], ay[3], ad[3];
        const float dts[3] = { 1.0f, 0.5f, 0.25f };
        const int steps[3] = { 120, 240, 480 };
        int k;

        for (k = 0; k < 3; k++) {
            JovSim s;
            int i;
            jov_sim_reset(&s, 11);
            for (i = 0; i < steps[k]; i++) jov_sim_update(&s, 1.0f, -1.0f, 0, dts[k]);
            ax[k] = s.player.x; ay[k] = s.player.y; ad[k] = s.rail.distance;
        }

        check_near(ax[1], ax[0], 1.2, "ship x agrees between 60Hz and 120Hz");
        check_near(ax[2], ax[0], 1.8, "ship x agrees between 60Hz and 240Hz");
        check_near(ay[1], ay[0], 1.2, "ship y agrees between 60Hz and 120Hz");
        check_near(ay[2], ay[0], 1.8, "ship y agrees between 60Hz and 240Hz");
        check_near(ad[1], ad[0], 0.5, "distance travelled agrees between 60Hz and 120Hz");
        check_near(ad[2], ad[0], 0.5, "distance travelled agrees between 60Hz and 240Hz");

        /* The pair this console actually ships on. dt is 1.0 at NTSC's 60Hz and
         * 1.2 at PAL's 50Hz, and 250 NTSC frames is 208.33 PAL ones -- so the
         * comparison is not quite exact and the tolerance covers the partial
         * frame rather than any drift. The web suite has no reason to carry
         * this check; here it is the whole reason the convention exists. */
        {
            JovSim n, p;
            int i;
            jov_sim_reset(&n, 15);
            jov_sim_reset(&p, 15);
            for (i = 0; i < 250; i++) jov_sim_update(&n, 0.0f, 0.0f, 0, 1.0f);
            for (i = 0; i < 208; i++) jov_sim_update(&p, 0.0f, 0.0f, 0, 1.2f);
            check_near(p.rail.distance, n.rail.distance, 2.0,
                       "NTSC and PAL cover the same rail in the same seconds");
        }

        /* Contact depth is what everything else is measured against, so it gets
         * its own check with no player input in play at all. */
        {
            float z[2];
            const float d[2] = { 1.0f, 0.5f };
            const int st[2] = { 100, 200 };
            for (k = 0; k < 2; k++) {
                JovSim s;
                int i;
                jov_sim_reset(&s, 12);
                push_contact(&s, JOV_HOSTILE, 0.0f, 0.0f, 900.0f);
                for (i = 0; i < st[k]; i++) jov_sim_update_contacts(&s, 5.0f, d[k]);
                z[k] = s.n_contacts > 0 ? s.contacts[0].z : -9999.0f;
            }
            check_near(z[1], z[0], 0.01, "contact depth is dt-invariant");
        }

        /* The camera chase is proportional, so it is the one most likely to be
         * got wrong with a plain multiply.
         *
         * TIGHTENED from the web suite's tolerance of 0.6. A correct
         * 1 - (1 - k)^dt compounds to exactly 1 - (1 - k)^n whatever dt is, so
         * the only error here is float rounding and the honest tolerance is
         * tiny. At 0.6 the obvious wrong version -- k = CAM_LAG * dt -- lands
         * 0.43 out and passes, which it did. */
        {
            float cam[3];
            const float d[3] = { 1.0f, 0.5f, 0.25f };
            const int st[3] = { 30, 60, 120 };
            for (k = 0; k < 3; k++) {
                JovSim s;
                int i;
                jov_sim_reset(&s, 13);
                s.player.x = 100.0f;
                for (i = 0; i < st[k]; i++) jov_rail_update(&s.rail, &s.player, 5.0f, d[k]);
                cam[k] = s.rail.cam_x;
            }
            check_near(cam[1], cam[0], 0.02, "camera drift is dt-invariant at half a step");
            check_near(cam[2], cam[0], 0.02, "and at a quarter of one");
        }

        /* Drag is a per-frame multiplier and must be raised to dt, not scaled
         * by it. */
        {
            float after[2];
            const float d[2] = { 1.0f, 0.5f };
            const int st[2] = { 20, 40 };
            for (k = 0; k < 2; k++) {
                JovSim s;
                int i;
                jov_sim_reset(&s, 14);
                for (i = 0; i < 20; i++) jov_sim_update(&s, 1.0f, 0.0f, 0, 1.0f);
                for (i = 0; i < st[k]; i++) jov_player_update(&s.player, 0.0f, 0.0f, 0, d[k]);
                after[k] = s.player.vx;
            }
            check_near(after[1], after[0], 0.05, "velocity decay is dt-invariant");
        }
    }

    /* =================================================================
     * 4. Projection
     * ================================================================= */
    section("projection");
    {
        float z;
        int monotonic = 1;
        float prev = 1e30f;

        check_near(proj_scale_at(0.0f), 1.0, 1e-9, "scale is exactly 1 at the ship plane");

        for (z = 0.0f; z <= Z_FAR; z += 10.0f) {
            float s = proj_scale_at(z);
            if (s >= prev) { monotonic = 0; break; }
            prev = s;
        }
        check(monotonic, "scale decreases monotonically with depth");
        check(proj_scale_at(Z_FAR) > 0.0f, "nothing on the rail projects to zero or behind");

        /* scale and depth must be true inverses, or the deck bands land at
         * depths that do not match where they are drawn.
         *
         * RELAXED from the web suite's 1e-6: this runs in float, where a
         * round trip through a reciprocal at z = 1100 cannot do better than
         * about 1e-4. 0.01 world units is a hundredth of a pixel at the ship
         * plane and far below anything the deck spacing can show. */
        {
            const float zs[5] = { 0.0f, 120.0f, 410.0f, 600.0f, 1100.0f };
            int i;
            for (i = 0; i < 5; i++) {
                check_near(proj_depth_at(proj_scale_at(zs[i])), zs[i], 0.01,
                           "depth inverts scale across the rail");
            }
        }

        /* The defining property of a vanishing point: the camera's own axis
         * lands on the same point at every depth. */
        {
            ProjPoint v = proj_vanishing(40.0f, 12.0f);
            const float zs[4] = { 0.0f, 200.0f, 600.0f, 1100.0f };
            int i, all_agree = 1;
            for (i = 0; i < 4; i++) {
                ProjPoint p = proj_point(40.0f, 12.0f, zs[i], 40.0f, 12.0f);
                if (fabsf(p.x - v.x) > 1e-4f || fabsf(p.y - v.y) > 1e-4f) {
                    all_agree = 0;
                    break;
                }
            }
            check(all_agree, "the camera axis lands on the vanishing point at every depth");
        }

        /* Aiming is done in world space so it cannot get easier up close. */
        check(proj_in_box(0, 0, 6, 0, 10, 6) && !proj_in_box(0, 0, 14, 0, 10, 6),
              "the box test is world-space, horizontally");
        check(proj_in_box(0, 0, 0, 5, 10, 6) && !proj_in_box(0, 0, 0, 9, 10, 6),
              "and vertically");
    }

    /* =================================================================
     * 4b. Hit boxes
     * ================================================================= */
    section("hit boxes -- generous on hostiles, honest on convoys");
    {
        JovContact hostile, aid;
        float hw, hh, aw, ah;
        memset(&hostile, 0, sizeof(hostile)); hostile.kind = JOV_HOSTILE;
        memset(&aid, 0, sizeof(aid)); aid.kind = JOV_AID;
        jov_hit_box(&hostile, &hw, &hh);
        jov_hit_box(&aid, &aw, &ah);

        /* The point of the change. A hostile must be at least as easy to hit as
         * a convoy, or every near miss punishes the player twice: the target
         * lives and the thing that ends the run takes the shot instead. */
        check_detail(hw >= aw, "a hostile is at least as wide a target as a convoy",
                     "hostile %g vs convoy %g", hw, aw);
        check_detail(hh >= ah, "and at least as tall", "hostile %g vs convoy %g", hh, ah);

        check(hw > HOSTILE_W / 2.0f, "the hostile box is more generous than its hull");
        check_detail(aw <= AID_W / 2.0f, "the convoy box never exceeds its hull",
                     "box %g vs hull %g", aw, AID_W / 2.0f);
        check(ah <= AID_H / 2.0f, "in both directions");

        /* Collision actually uses it: a shot just outside a convoy's hull
         * misses. */
        {
            JovSim s;
            jov_sim_reset(&s, 62);
            push_contact(&s, JOV_AID, 0.0f, 0.0f, 200.0f)->pinged = 1;
            push_shot(&s, AID_W / 2.0f + 3.0f, 0.0f, 190.0f);
            jov_sim_update_shots(&s, 1.0f);
            check(!has_event(&s, JOV_EV_FRIENDLY_FIRE),
                  "a shot past the edge of a convoy does not clip it");
        }

        /* And the nearest candidate wins, not the earliest in the array --
         * otherwise friendly fire is decided by allocation order. */
        {
            JovSim s;
            jov_sim_reset(&s, 63);
            push_contact(&s, JOV_AID, 0.0f, 0.0f, 210.0f);      /* spawned first */
            push_contact(&s, JOV_HOSTILE, 0.0f, 0.0f, 200.0f);  /* but nearer */
            push_shot(&s, 0.0f, 0.0f, 190.0f);
            jov_sim_update_shots(&s, 1.0f);
            check(has_event(&s, JOV_EV_HOSTILE_KILLED)
                  && !has_event(&s, JOV_EV_FRIENDLY_FIRE),
                  "a shot takes the nearest contact, not the oldest");
        }
    }

    /* =================================================================
     * 4c. The rescue window
     * ================================================================= */
    section("rescue window -- long enough to win, short enough to matter");
    {
        /* FLOOR, flown rather than assumed: put the ship in one corner of the
         * rail and drive it to the opposite one with the real flight code, then
         * add the shot's flight, a reaction and a full gun cooldown. That is
         * the longest a perfect player can take to answer a lock. */
        JovPlayer p;
        int cross = 0;
        float intercept, rail_life;

        jov_player_reset(&p);
        p.x = -SPAWN_X_RANGE;
        p.y = SHIP_Y_MAX;
        while ((p.x < SPAWN_X_RANGE - 2.0f || p.y > -CONTACT_Y_SPREAD + 2.0f)
               && cross < 2000) {
            jov_player_update(&p, 1.0f, -1.0f, 0, 1.0f);
            cross++;
        }
        intercept = (float)cross + Z_FIRE_MAX / SHOT_SPEED + REACTION_FRAMES + SHOT_COOLDOWN;

        check_detail(AID_KILL_FRAMES > intercept,
                     "a convoy outlives the worst-case intercept, so the rescue is possible",
                     "timer %.0f vs intercept %.0f", AID_KILL_FRAMES, intercept);

        /* CEILING: a timer longer than the rail is no timer at all, because the
         * convoy reaches the camera and escapes before it can expire. */
        rail_life = (Z_FAR - Z_NEAR) / RAIL_SPEED_MAX;
        check_detail(AID_KILL_FRAMES < rail_life,
                     "and dies before it could simply fly off the end, so the threat is real",
                     "timer %.0f vs rail life %.0f", AID_KILL_FRAMES, rail_life);

        /* The two bounds have to leave room for each other. If a retune ever
         * closes this gap the mechanic cannot be made fair and meaningful at
         * once, and the fix is a faster ship or a shorter rail, not a bigger
         * number here. */
        check_detail(rail_life > intercept,
                     "the rail is long enough for an intercept to fit inside it at all",
                     "rail %.0f vs intercept %.0f", rail_life, intercept);
    }

    /* =================================================================
     * 5. Spawning
     * ================================================================= */
    section("spawning");
    {
        /* Separation is what stops a convoy being hidden behind a hostile at
         * the exact moment identification matters. */
        float worst = 1e30f;
        int seed;
        for (seed = 0; seed < 40; seed++) {
            JovSim s;
            int i, j;
            jov_sim_reset(&s, (unsigned int)(500 + seed));
            jov_sim_spawn_wave(&s, 1.0f);
            for (i = 0; i < s.n_contacts; i++) {
                for (j = i + 1; j < s.n_contacts; j++) {
                    float d = fabsf(s.contacts[i].x - s.contacts[j].x);
                    if (d < worst) worst = d;
                }
            }
        }
        /* The full separation, not a fraction of it: jov_spread_x guarantees
         * this by construction, so anything less means the construction was
         * replaced by something that can fail. */
        check_detail(worst >= SPAWN_MIN_SEPARATION - 1e-3f,
                     "waves keep their contacts laterally apart",
                     "closest pair over 40 waves: %.1f, need %.0f",
                     worst, SPAWN_MIN_SEPARATION);

        /* Every contact spawns inside the box the ship can actually reach
         * across, or some of them can never be engaged at all. The web suite
         * picks t at random; a sweep is the same coverage without the dice. */
        {
            int all_in_range = 1;
            for (seed = 0; seed < 30; seed++) {
                JovSim s;
                int i;
                jov_sim_reset(&s, (unsigned int)(700 + seed));
                jov_sim_spawn_wave(&s, (float)seed / 29.0f);
                for (i = 0; i < s.n_contacts; i++) {
                    if (fabsf(s.contacts[i].x) > SPAWN_X_RANGE + 1.0f) all_in_range = 0;
                }
            }
            check(all_in_range, "contacts spawn within the rail the ship can cover");
        }

        /* A screen with nothing hostile on it is a stalled game, and the
         * opening wave is the entire tutorial: it must show one of each, at
         * every seed, or some runs open by teaching that everything is a
         * target. */
        {
            int always_both = 1, always_hostile = 1;
            for (seed = 0; seed < 50; seed++) {
                JovSim s;
                int i, aid = 0, hostile = 0;
                jov_sim_reset(&s, (unsigned int)(1000 + seed));
                jov_sim_spawn_wave(&s, 0.0f);
                for (i = 0; i < s.n_contacts; i++) {
                    if (s.contacts[i].kind == JOV_AID) aid++; else hostile++;
                }
                if (!hostile) always_hostile = 0;
                if (!aid || !hostile) always_both = 0;
            }
            check(always_hostile, "the first wave always contains something to shoot");
            check(always_both, "the opening wave always shows one convoy and one hostile");
        }

        /* And they must be far enough apart to be told apart. */
        {
            float opening_gap = 1e30f;
            for (seed = 0; seed < 50; seed++) {
                JovSim s;
                float d;
                jov_sim_reset(&s, (unsigned int)(2000 + seed));
                jov_sim_spawn_wave(&s, 0.0f);
                d = fabsf(s.contacts[0].x - s.contacts[1].x);
                if (d < opening_gap) opening_gap = d;
            }
            check_detail(opening_gap >= SPAWN_MIN_SEPARATION - 1e-3f,
                         "and they are separated on the opening wave too",
                         "closest opening pair: %.1f, need %.0f",
                         opening_gap, SPAWN_MIN_SEPARATION);
        }
    }

    /* =================================================================
     * 6. Flight and survivability
     * ================================================================= */
    section("flight");
    {
        /* The ship must be able to cross the spawn range faster than a contact
         * crosses the rail, or some spawns are unreachable by construction. */
        {
            JovPlayer p;
            int frames = 0;
            float crossing;
            jov_player_reset(&p);
            p.x = -SPAWN_X_RANGE;
            while (p.x < SPAWN_X_RANGE && frames < 600) {
                jov_player_update(&p, 1.0f, 0.0f, 0, 1.0f);
                frames++;
            }
            crossing = Z_FAR / jov_diff_rail_speed(1.0f);
            check_detail((float)frames < crossing,
                         "the ship can cross the full spawn width before a contact crosses the rail",
                         "%.0f frames to cross vs %.0f frames of rail",
                         (double)frames, crossing);
        }

        /* Walls hold under a held stick. */
        {
            JovPlayer p;
            int i;
            jov_player_reset(&p);
            for (i = 0; i < 400; i++) jov_player_update(&p, 1.0f, 1.0f, 0, 1.0f);
            check(fabsf(p.x) <= SHIP_X_RANGE + 0.001f, "the ship cannot leave the rail sideways");
            check(p.y <= SHIP_Y_MAX + 0.001f, "the ship cannot leave the rail vertically");
        }

        /* The gun honours its cooldown rather than firing every frame. */
        {
            JovPlayer p;
            int i, shots = 0;
            jov_player_reset(&p);
            for (i = 0; i < 90; i++) if (jov_player_update(&p, 0.0f, 0.0f, 1, 1.0f)) shots++;
            check_detail(shots <= (int)ceilf(90.0f / SHOT_COOLDOWN) + 1 && shots > 1,
                         "held fire respects the cooldown",
                         "%.0f shots in 90 frames, cap %.0f",
                         (double)shots, (double)((int)ceilf(90.0f / SHOT_COOLDOWN) + 1));
        }

        /* i-frames actually protect. */
        {
            JovPlayer p;
            jov_player_reset(&p);
            check(jov_player_take_hit(&p), "the first hit lands");
            check(!jov_player_take_hit(&p), "a second hit inside the invincibility window does not");
            check(p.lives == MAX_LIVES - 1, "and only one ship was spent");
        }
    }

    /* =================================================================
     * 7. Capacity -- the cost of not having growable arrays
     *
     * This has no counterpart in the web suite, and it is the risk the port
     * introduced. The browser pushes onto arrays that grow; every array here is
     * fixed, so a wave that will not fit is refused whole and an event raised
     * with the queue full is discarded. A discarded event is an unpaid score,
     * which is invisible: the contact still dies, the explosion still plays,
     * and the number at the top of the screen is quietly light.
     * ================================================================= */
    section("capacity -- nothing is silently dropped in a real run");
    {
        JovSim s;
        JovRun run;
        JovCue cue;
        int f, peak_contacts = 0, peak_particles = 0;

        jov_sim_reset(&s, 41);
        jov_run_reset(&run);
        /* Fly badly on purpose: firing constantly maximises kills, explosions
         * and events per frame, which is the worst case for all three caps. */
        for (f = 0; f < 6000; f++) {
            jov_sim_update(&s, 0.4f, -0.2f, 1, 1.0f);
            jov_run_resolve(&run, &s, &cue);
            jov_run_tick(&run, 1.0f);
            if (s.n_contacts > peak_contacts) peak_contacts = s.n_contacts;
            if (s.n_particles > peak_particles) peak_particles = s.n_particles;
            /* The run ends where the game would end it; keep flying anyway, so
             * the caps are exercised past the point a player could reach. */
            if (s.player.lives <= 0) s.player.lives = MAX_LIVES;
            if (run.strikes >= MAX_STRIKES) run.strikes = 0;
        }

        check_detail(s.events_dropped == 0,
                     "no event was dropped over 6000 frames of the worst case",
                     "%.0f dropped, want %.0f", (double)s.events_dropped, 0.0);
        check_detail(s.waves_dropped == 0,
                     "and no wave was refused for want of room",
                     "%.0f refused, want %.0f", (double)s.waves_dropped, 0.0);
        check_detail(peak_contacts < MAX_CONTACTS,
                     "the contact array kept headroom",
                     "peak %.0f of %.0f", (double)peak_contacts, (double)MAX_CONTACTS);
        check_detail(peak_particles <= MAX_PARTICLES,
                     "and the particle array stayed inside itself",
                     "peak %.0f of %.0f", (double)peak_particles, (double)MAX_PARTICLES);

        /* The depth ordering is what both the draw order and the HUD strip read,
         * so it has to be a real ordering and not merely a list. */
        {
            int order[MAX_CONTACTS];
            int n = jov_sim_order_by_depth(&s, order);
            int i, sorted = 1;
            for (i = 1; i < n; i++) {
                if (s.contacts[order[i]].z > s.contacts[order[i - 1]].z) { sorted = 0; break; }
            }
            check(n == s.n_contacts, "the depth ordering lists every live contact");
            check(sorted, "far to near, so nearer contacts draw over what is behind them");
        }
    }

    /* -- Summary ------------------------------------------------------ */
    printf("\n%d checks, %d failed\n\n", checks, failures);
    return failures ? 1 : 0;
}
