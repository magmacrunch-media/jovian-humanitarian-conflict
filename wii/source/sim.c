/* =====================================================================
 * sim.c -- The Jovian Humanitarian Conflict (Wii)
 *
 * A port of web/js/player.js, world.js and entities.js, plus the scoring
 * policy out of main.js. See sim.h for the contract and for the three places
 * this deviates from the browser.
 *
 * Sections below are marked with the web file they came from, so a rules
 * change over there can be carried across by reading one section rather than
 * the whole file. A rules change is not done until every version that exists
 * has it, or the commit says why one is skipped.
 *
 * NOTHING in this translation unit may include grrlib.h, ogc/ or magnolia.h.
 * ===================================================================== */
#include <math.h>
#include <string.h>

#include "sim.h"

/* -- small helpers ---------------------------------------------------- */

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* JavaScript's Math.sign, which returns 0 at 0. A plain (a < b ? -1 : 1)
 * would nudge a hostile that is exactly on its target, forever. */
static float signf_js(float v) {
    return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
}

float jov_lerp(float a, float b, float t) { return a + (b - a) * t; }

/* =====================================================================
 * Determinism -- the web suite's mulberry32, in integers
 * ===================================================================== */

void jov_rng_seed(JovRng *r, unsigned int seed) {
    r->a = seed;
}

/* The reference, for comparison when this is next touched:
 *
 *     a |= 0; a = (a + 0x6D2B79F5) | 0;
 *     let t = Math.imul(a ^ (a >>> 15), 1 | a);
 *     t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
 *     return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
 *
 * JavaScript's `|0` and `>>>` are the same bits as unsigned 32-bit wraparound
 * and a logical shift, and Math.imul is the low 32 bits of the product, so
 * this is the same sequence and not merely a similar one. `unsigned int` is
 * 32 bits on both gcc targets in play here -- the host and devkitPPC.
 */
float jov_rng_next(JovRng *r) {
    unsigned int a = r->a + 0x6D2B79F5u;
    unsigned int t;
    r->a = a;
    t = (a ^ (a >> 15)) * (1u | a);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    /* Divided in double and then narrowed: the quotient is exact in double,
     * so this is the nearest float to the browser's value rather than the
     * result of a different division. */
    return (float)((double)(t ^ (t >> 14)) / 4294967296.0);
}

/* =====================================================================
 * Difficulty -- web/js/config.js
 * ===================================================================== */

float jov_diff_at(float distance) {
    return clampf(distance / DIFFICULTY_DISTANCE, 0.0f, 1.0f);
}

float jov_diff_rail_speed(float t)     { return jov_lerp(RAIL_SPEED_MIN, RAIL_SPEED_MAX, t); }
float jov_diff_spawn_interval(float t) { return jov_lerp(SPAWN_INTERVAL_EARLY, SPAWN_INTERVAL_LATE, t); }
float jov_diff_aid_share(float t)      { return jov_lerp(AID_SHARE_EARLY, AID_SHARE_LATE, t); }
float jov_diff_aggro(float t)          { return jov_lerp(HOSTILE_AGGRO_EARLY, HOSTILE_AGGRO_LATE, t); }

/* Contacts released together. The fractional part is a per-wave coin flip. */
int jov_diff_wave_size(float t, JovRng *rng) {
    float raw = jov_lerp(WAVE_SIZE_EARLY, WAVE_SIZE_LATE, t);
    int base = (int)floorf(raw);
    return base + (jov_rng_next(rng) < raw - (float)base ? 1 : 0);
}

float jov_diff_identify_frames(float t) {
    return (Z_FAR - Z_FIRE_MAX) / jov_diff_rail_speed(t);
}

/* =====================================================================
 * The ship -- web/js/player.js
 * ===================================================================== */

void jov_player_reset(JovPlayer *p) {
    memset(p, 0, sizeof(*p));
    p->y = SHIP_Y_START;
    p->lives = MAX_LIVES;
}

int jov_player_update(JovPlayer *p, float axis_x, float axis_y,
                      int wants_fire, float dt) {
    float drag, max, target_bank;

    /* Accelerate toward the stick, then bleed off. Drag is a per-frame
     * multiplier, so it is RAISED to dt rather than multiplied by it -- the
     * difference is invisible at 60Hz and a third of the top speed at 144Hz,
     * and on this machine it is the difference between NTSC and PAL. */
    p->vx += axis_x * SHIP_ACCEL * dt;
    p->vy += axis_y * SHIP_ACCEL * dt;
    drag = powf(SHIP_DRAG, dt);
    p->vx *= drag;
    p->vy *= drag;

    max = SHIP_SPEED_MAX;
    p->vx = clampf(p->vx, -max, max);
    p->vy = clampf(p->vy, -max, max);

    p->x += p->vx * dt;
    p->y += p->vy * dt;

    /* Hard walls rather than a wrap: the rail has edges, and a ship that
     * reappeared on the far side would break the parallax it is driving.
     * Zeroing the velocity on contact stops it creeping while held. */
    if (p->x < -SHIP_X_RANGE) { p->x = -SHIP_X_RANGE; p->vx = 0.0f; }
    if (p->x >  SHIP_X_RANGE) { p->x =  SHIP_X_RANGE; p->vx = 0.0f; }
    if (p->y < SHIP_Y_MIN) { p->y = SHIP_Y_MIN; p->vy = 0.0f; }
    if (p->y > SHIP_Y_MAX) { p->y = SHIP_Y_MAX; p->vy = 0.0f; }

    /* Bank chases the lateral speed rather than the stick, so it settles a
     * beat after you stop turning instead of snapping flat. */
    target_bank = (p->vx / max) * BANK_MAX;
    p->bank += (target_bank - p->bank) * fminf(1.0f, 0.18f * dt);

    p->thrust_phase += dt;
    if (p->invincible > 0.0f) p->invincible -= dt;
    if (p->shoot_cooldown > 0.0f) p->shoot_cooldown -= dt;

    if (wants_fire && p->shoot_cooldown <= 0.0f) {
        p->shoot_cooldown = SHOT_COOLDOWN;
        return 1;
    }
    return 0;
}

void jov_player_muzzle(const JovPlayer *p, float *out_x, float *out_y) {
    *out_x = p->x;
    *out_y = p->y - 2.0f;
}

int jov_player_can_be_hit(const JovPlayer *p) {
    return p->invincible <= 0.0f;
}

int jov_player_take_hit(JovPlayer *p) {
    if (!jov_player_can_be_hit(p)) return 0;
    p->lives -= 1;
    p->invincible = INVINCIBLE_FRAMES;
    return 1;
}

/* =====================================================================
 * The rail -- web/js/world.js (the simulation half; the drawing is render.c)
 * ===================================================================== */

void jov_rail_reset(JovRail *r) {
    int i;
    r->distance = 0.0f;
    r->cam_x = 0.0f;
    r->cam_y = 0.0f;
    /* Deck bands recycle rather than being spawned and culled: a fixed ring of
     * depths, each wrapping back as it passes the camera. Constant memory, no
     * allocation in the loop, and the spacing cannot drift. */
    for (i = 0; i < RAIL_BANDS; i++) {
        r->bands[i] = DECK_Z_SPAN * ((float)i / (float)RAIL_BANDS);
    }
}

void jov_rail_update(JovRail *r, const JovPlayer *p, float rail_speed, float dt) {
    int i;
    float follow_x, follow_y, k;

    r->distance += rail_speed * dt;

    /* The camera trails the ship by a fraction of the gap per frame. Because
     * that is a proportional chase rather than a fixed step, it has to be
     * raised to dt -- a straight multiply overshoots badly at low frame rates
     * and the horizon visibly wobbles. */
    follow_x = p->x;
    follow_y = p->y * 0.5f;
    k = 1.0f - powf(1.0f - CAM_LAG, dt);
    r->cam_x += (follow_x - r->cam_x) * k;
    r->cam_y += (follow_y - r->cam_y) * k;

    for (i = 0; i < RAIL_BANDS; i++) {
        r->bands[i] -= rail_speed * dt;
        /* Wrap by ADDING the span rather than assigning it, so the even
         * spacing survives a large dt instead of collapsing into a clump. */
        while (r->bands[i] <= 0.0f) r->bands[i] += DECK_Z_SPAN;
    }
}

void jov_rail_drift(JovRail *r, float frame) {
    int i;
    r->cam_x = sinf(frame * 0.006f) * 40.0f;
    r->cam_y = sinf(frame * 0.004f) * 10.0f;
    for (i = 0; i < RAIL_BANDS; i++) {
        r->bands[i] -= RAIL_SPEED_MIN * 0.5f;
        while (r->bands[i] <= 0.0f) r->bands[i] += DECK_Z_SPAN;
    }
}

/* =====================================================================
 * Contacts -- web/js/entities.js
 * ===================================================================== */

static void push_event(JovSim *s, JovEventType type, const JovContact *c) {
    JovEvent *e;
    if (s->n_events >= MAX_EVENTS) { s->events_dropped++; return; }
    e = &s->events[s->n_events++];
    e->type = type;
    e->contact_id = c->id;
    e->contact_kind = c->kind;
    e->x = c->x;
    e->y = c->y;
    e->z = c->z;
}

void jov_sim_reset(JovSim *s, unsigned int seed) {
    memset(s, 0, sizeof(*s));
    jov_player_reset(&s->player);
    jov_rail_reset(&s->rail);
    s->next_id = 1;
    jov_rng_seed(&s->rng, seed);
}

/* -- Spawning --------------------------------------------------------- */

void jov_spread_x(int n, JovRng *rng, float *out) {
    const float W = SPAWN_X_RANGE * 2.0f;
    float gap, slack;
    int i, j;

    if (n <= 0) return;

    /* Should a wave ever be large enough that the full gaps cannot fit, share
     * the rail out evenly instead of returning positions that break the rule. */
    gap = n > 1 ? W / (float)(n - 1) : W;
    if (SPAWN_MIN_SEPARATION < gap) gap = SPAWN_MIN_SEPARATION;
    slack = W - (float)(n - 1) * gap;
    if (slack < 0.0f) slack = 0.0f;

    for (i = 0; i < n; i++) out[i] = jov_rng_next(rng) * slack;

    /* Insertion sort. n is 2 or 3. */
    for (i = 1; i < n; i++) {
        float v = out[i];
        for (j = i - 1; j >= 0 && out[j] > v; j--) out[j + 1] = out[j];
        out[j + 1] = v;
    }

    for (i = 0; i < n; i++) out[i] += -SPAWN_X_RANGE + (float)i * gap;
}

void jov_sim_spawn_wave(JovSim *s, float t) {
    float xs[MAX_CONTACTS];
    JovKind kinds[MAX_CONTACTS];
    float aid_share, aggro_t;
    int n, i, none_hostile, has_hostile;

    /* The opening wave is scripted to show one of each, so it needs two slots
     * regardless of what the curve would have chosen. */
    n = s->waves_spawned == 0 ? 2 : jov_diff_wave_size(t, &s->rng);
    if (n <= 0) return;
    if (n > MAX_CONTACTS) n = MAX_CONTACTS;

    /* A wave that does not fit is refused WHOLE. Spawning the half that fits
     * would break the separation guarantee, which is the one property this
     * function exists to provide. */
    if (s->n_contacts + n > MAX_CONTACTS) { s->waves_dropped++; return; }

    jov_spread_x(n, &s->rng, xs);
    aid_share = jov_diff_aid_share(t);

    for (i = 0; i < n; i++) {
        kinds[i] = jov_rng_next(&s->rng) < aid_share ? JOV_AID : JOV_HOSTILE;
    }

    if (s->waves_spawned == 0) {
        /* The opening wave is one of each, always.
         *
         * Left to the dice at a 30% convoy share and two per wave, the first
         * convoy took about fifteen seconds to turn up -- so the game opened
         * by teaching that everything in the sky is a target, and only then
         * introduced the one rule that contradicts it. Showing both side by
         * side in the first wave, with the separation rule guaranteeing a gap
         * between them, is the whole tutorial this game needs. */
        kinds[0] = JOV_HOSTILE;
        kinds[1] = JOV_AID;
    } else {
        /* A wave with nothing to shoot, on a screen with nothing to shoot,
         * leaves the player holding fire at empty sky -- which reads as the
         * game having stalled rather than as restraint being asked of them. */
        none_hostile = 1;
        for (i = 0; i < s->n_contacts; i++) {
            if (s->contacts[i].kind == JOV_HOSTILE && !s->contacts[i].dead) {
                none_hostile = 0;
                break;
            }
        }
        has_hostile = 0;
        for (i = 0; i < n; i++) if (kinds[i] == JOV_HOSTILE) { has_hostile = 1; break; }
        if (none_hostile && !has_hostile) kinds[n - 1] = JOV_HOSTILE;
    }
    s->waves_spawned++;

    /* jov_spread_x returns positions in ascending order, so pairing them with
     * kinds directly would put convoys on the left of every wave. Shuffle. */
    for (i = n - 1; i > 0; i--) {
        int j = (int)floorf(jov_rng_next(&s->rng) * (float)(i + 1));
        float tmp;
        if (j > i) j = i;              /* only reachable if next() ever returns 1.0 */
        tmp = xs[i]; xs[i] = xs[j]; xs[j] = tmp;
    }

    for (i = 0; i < n; i++) {
        JovContact *c = &s->contacts[s->n_contacts++];
        int is_aid = kinds[i] == JOV_AID;
        memset(c, 0, sizeof(*c));
        c->id = s->next_id++;
        c->kind = is_aid ? JOV_AID : JOV_HOSTILE;
        c->x = xs[i];
        c->y = (jov_rng_next(&s->rng) * 2.0f - 1.0f) * CONTACT_Y_SPREAD;
        c->z = Z_FAR;
        /* Drift phase is seeded per contact so a wave does not weave in
         * lockstep, which looks mechanical. */
        c->phase = jov_rng_next(&s->rng) * 3.14159265358979f * 2.0f;
        c->drift_seed = 0.6f + jov_rng_next(&s->rng) * 0.8f;
        /* Short-circuit deliberately: a convoy must NOT consume a random here,
         * or the whole sequence diverges from the browser's. */
        aggro_t = jov_diff_aggro(t);
        c->aggro = (!is_aid) && (jov_rng_next(&s->rng) < aggro_t);
    }
}

/* -- Effects ---------------------------------------------------------- */

static void explode(JovSim *s, const JovContact *c, unsigned int color) {
    int i;
    for (i = 0; i < PARTICLE_COUNT; i++) {
        float a = (3.14159265358979f * 2.0f * (float)i) / (float)PARTICLE_COUNT;
        JovParticle *p;
        if (s->n_particles >= MAX_PARTICLES) {
            /* Evict oldest. A lost sparkle is a lost sparkle. */
            memmove(&s->particles[0], &s->particles[1],
                    sizeof(JovParticle) * (MAX_PARTICLES - 1));
            s->n_particles = MAX_PARTICLES - 1;
        }
        p = &s->particles[s->n_particles++];
        p->x = c->x; p->y = c->y; p->z = c->z;
        p->vx = cosf(a) * PARTICLE_SPEED;
        p->vy = sinf(a) * PARTICLE_SPEED;
        p->life = PARTICLE_LIFE;
        p->max = PARTICLE_LIFE;
        p->color = color;
    }
}

void jov_sim_add_popup(JovSim *s, float x, float y, float z,
                       const char *text, unsigned int color) {
    JovPopup *p;
    if (s->n_popups >= MAX_POPUPS) {
        memmove(&s->popups[0], &s->popups[1], sizeof(JovPopup) * (MAX_POPUPS - 1));
        s->n_popups = MAX_POPUPS - 1;
    }
    p = &s->popups[s->n_popups++];
    p->x = x; p->y = y; p->z = z;
    p->color = color;
    p->life = 46.0f;
    p->max = 46.0f;
    strncpy(p->text, text, POPUP_TEXT_MAX - 1);
    p->text[POPUP_TEXT_MAX - 1] = '\0';
}

static void update_particles(JovSim *s, float dt) {
    int i, kept = 0;
    for (i = 0; i < s->n_particles; i++) {
        JovParticle *p = &s->particles[i];
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->life -= dt;
        if (p->life > 0.0f) s->particles[kept++] = *p;
    }
    s->n_particles = kept;
}

static void update_popups(JovSim *s, float dt) {
    int i, kept = 0;
    for (i = 0; i < s->n_popups; i++) {
        JovPopup *p = &s->popups[i];
        p->y -= 0.5f * dt;
        p->life -= dt;
        if (p->life > 0.0f) s->popups[kept++] = *p;
    }
    s->n_popups = kept;
}

/* -- Per-frame -------------------------------------------------------- */

static JovContact *nearest_aid(JovSim *s, const JovContact *from) {
    JovContact *best = 0;
    float best_d = 1e30f;
    int i;
    for (i = 0; i < s->n_contacts; i++) {
        JovContact *c = &s->contacts[i];
        float d;
        if (c->kind != JOV_AID || c->dead) continue;
        d = fabsf(c->z - from->z);
        /* Strictly less-than, so a tie goes to the earlier contact -- which is
         * spawn order, and is the browser's behaviour. */
        if (d < best_d) { best_d = d; best = c; }
    }
    return best;
}

static JovContact *find_live(JovSim *s, int id) {
    int i;
    if (id == 0) return 0;
    for (i = 0; i < s->n_contacts; i++) {
        if (s->contacts[i].id == id && !s->contacts[i].dead) return &s->contacts[i];
    }
    return 0;
}

const JovContact *jov_sim_find(const JovSim *s, int id) {
    return find_live((JovSim *)s, id);
}

void jov_sim_update_contacts(JovSim *s, float rail_speed, float dt) {
    int i, kept;

    for (i = 0; i < s->n_contacts; i++) {
        JovContact *c = &s->contacts[i];
        float drift;

        c->age += dt;
        c->z -= rail_speed * dt;
        c->phase += 0.04f * dt * c->drift_seed;

        drift = c->kind == JOV_AID ? AID_DRIFT : HOSTILE_DRIFT;
        c->x += cosf(c->phase) * drift * dt;

        /* A convoy announces itself once, on the way in. main.c turns this
         * into the transponder ping; the simulation only reports that the
         * contact crossed the line, so this stays testable and audio-free. */
        if (c->kind == JOV_AID && !c->pinged && c->z <= Z_PING) {
            c->pinged = 1;
            push_event(s, JOV_EV_AID_SIGHTED, c);
        }

        /* An aggressive hostile slides toward the nearest convoy rather than
         * weaving, which is what makes escorting an active job: the convoy is
         * not merely something you refrain from shooting, it is something
         * being shot at. */
        if (c->kind == JOV_HOSTILE && c->aggro) {
            JovContact *target = nearest_aid(s, c);
            if (target) {
                c->x += signf_js(target->x - c->x) * HOSTILE_DRIFT * 0.55f * dt;
                if (fabsf(target->x - c->x) < 28.0f && target->doom_timer <= 0.0f
                    && fabsf(target->z - c->z) < LOCK_MAX_DZ) {
                    target->doom_timer = AID_KILL_FRAMES;
                    target->locked_by = c->id;
                    push_event(s, JOV_EV_AID_LOCKED, target);
                }
            }
        }

        /* A convoy under fire dies unless its attacker does first. Clearing
         * the lock when the attacker is gone is what makes the rescue land. */
        if (c->kind == JOV_AID && c->doom_timer > 0.0f) {
            JovContact *attacker = find_live(s, c->locked_by);
            if (!attacker) {
                c->doom_timer = 0.0f;
                c->locked_by = 0;
            } else {
                c->doom_timer -= dt;
                if (c->doom_timer <= 0.0f) {
                    c->dead = 1;
                    explode(s, c, C_AID);
                    push_event(s, JOV_EV_AID_LOST, c);
                }
            }
        }

        /* Collision with the player, in the z = 0 plane. Only hostiles ram;
         * flying through a convoy is not a punishable act. */
        if (!c->dead && c->kind == JOV_HOSTILE && c->z < 14.0f && c->z > -14.0f
            && jov_player_can_be_hit(&s->player)) {
            if (fabsf(c->x - s->player.x) < (HOSTILE_W + SHIP_W) / 2.0f &&
                fabsf(c->y - s->player.y) < (HOSTILE_H + SHIP_H) / 2.0f) {
                c->dead = 1;
                explode(s, c, C_HOSTILE);
                push_event(s, JOV_EV_PLAYER_HIT, c);
            }
        }
    }

    /* Retire what has passed the camera. A convoy that makes it out the far
     * side alive is an escort earned. */
    kept = 0;
    for (i = 0; i < s->n_contacts; i++) {
        JovContact *c = &s->contacts[i];
        if (c->dead) continue;
        if (c->z <= Z_NEAR) {
            if (c->kind == JOV_AID) push_event(s, JOV_EV_AID_ESCORTED, c);
            continue;
        }
        s->contacts[kept++] = *c;
    }
    s->n_contacts = kept;
}

/* -- Shots ------------------------------------------------------------ */

void jov_sim_fire(JovSim *s) {
    float mx, my;
    if (s->n_shots >= MAX_SHOTS) return;   /* SHOT_COOLDOWN makes this unreachable */
    jov_player_muzzle(&s->player, &mx, &my);
    s->shots[s->n_shots].x = mx;
    s->shots[s->n_shots].y = my;
    s->shots[s->n_shots].z = 0.0f;
    s->n_shots++;
}

void jov_hit_box(const JovContact *c, float *half_w, float *half_h) {
    int is_aid = c->kind == JOV_AID;
    *half_w = (is_aid ? AID_W : HOSTILE_W) / 2.0f
            + (is_aid ? AID_HIT_MARGIN_W : HOSTILE_HIT_MARGIN_W);
    *half_h = (is_aid ? AID_H : HOSTILE_H) / 2.0f
            + (is_aid ? AID_HIT_MARGIN_H : HOSTILE_HIT_MARGIN_H);
}

/*
 * Advance shots and resolve hits.
 *
 * The test is done in world space at the shot's own depth, not on screen, so a
 * target centred under the reticle is centred at every distance. Doing it on
 * screen would make near targets easier to hit purely because their sprites
 * are bigger, which would quietly punish holding fire -- exactly backwards for
 * this game.
 */
void jov_sim_update_shots(JovSim *s, float dt) {
    const float HULL_Z = 10.0f;
    int i, j, kept = 0;

    for (i = 0; i < s->n_shots; i++) {
        JovShot *sh = &s->shots[i];
        float from_z = sh->z;
        JovContact *hit = 0;

        sh->z += SHOT_SPEED * dt;
        if (from_z > Z_FIRE_MAX) continue;   /* retired, not kept */

        /* Sweep the depth the shot covered this frame rather than testing the
         * single point it ended on: at SHOT_SPEED 26 a shot skips 26 units per
         * frame, which is wider than a hull, so a point test lets shots pass
         * through contacts at some frame rates and not others. */
        for (j = 0; j < s->n_contacts; j++) {
            JovContact *c = &s->contacts[j];
            float hw, hh;
            if (c->dead) continue;
            if (c->z < from_z - HULL_Z || c->z > sh->z + HULL_Z) continue;
            jov_hit_box(c, &hw, &hh);
            if (!proj_in_box(sh->x, sh->y, c->x, c->y, hw, hh)) continue;
            /* Take the NEAREST candidate, not the first one in the array.
             * Array order is spawn order, so without this a convoy that
             * happened to spawn earlier would absorb a shot aimed at a hostile
             * in front of it -- friendly fire decided by allocation order,
             * which is both unfair and untestable. */
            if (!hit || c->z < hit->z) hit = c;
        }

        if (hit) {
            hit->dead = 1;
            if (hit->kind == JOV_AID) {
                explode(s, hit, C_AID);
                /* Attributed to the player, not to the hostiles. The penalty
                 * is severe enough that getting this wrong would be the
                 * cruellest bug in the game, so it is asserted by test. */
                push_event(s, JOV_EV_FRIENDLY_FIRE, hit);
            } else {
                explode(s, hit, C_HOSTILE);
                push_event(s, JOV_EV_HOSTILE_KILLED, hit);
            }
            continue;
        }
        s->shots[kept++] = *sh;
    }
    s->n_shots = kept;

    /* Dead contacts are removed here as well as in update_contacts, so a kill
     * cannot be scored twice by a second shot arriving the same frame. */
    kept = 0;
    for (i = 0; i < s->n_contacts; i++) {
        if (!s->contacts[i].dead) s->contacts[kept++] = s->contacts[i];
    }
    s->n_contacts = kept;
}

/* -- The tick --------------------------------------------------------- */

void jov_sim_update(JovSim *s, float axis_x, float axis_y, int wants_fire,
                    float dt) {
    float t, rail_speed;

    s->frame += dt;

    /* Difficulty is read BEFORE the rail advances, so it lags the distance by
     * one frame. That is what the browser does; the port keeps it rather than
     * quietly improving on it, because "the same numbers" has to mean the same
     * numbers in the same order. */
    t = jov_diff_at(s->rail.distance);
    rail_speed = jov_diff_rail_speed(t);

    if (jov_player_update(&s->player, axis_x, axis_y, wants_fire, dt)) {
        jov_sim_fire(s);
    }

    jov_rail_update(&s->rail, &s->player, rail_speed, dt);

    s->spawn_timer -= dt;
    if (s->spawn_timer <= 0.0f) {
        jov_sim_spawn_wave(s, t);
        s->spawn_timer = jov_diff_spawn_interval(t);
    }

    jov_sim_update_contacts(s, rail_speed, dt);
    jov_sim_update_shots(s, dt);
    update_particles(s, dt);
    update_popups(s, dt);
}

/* -- IFF and ordering -------------------------------------------------- */

int jov_beacon_lit(const JovContact *c, float frame) {
    int p;
    if (c->kind != JOV_AID) return 0;
    /* The browser's `frame` is an integer counter; here it accumulates dt, so
     * it is floored before the modulo. At dt = 1 the two are the same
     * sequence; at PAL's dt = 1.2 the blink runs on wall-clock time, which is
     * the behaviour the fairness budget is stated in. */
    p = ((int)floorf(frame)) % BLINK_PERIOD;
    if (p < 0) p += BLINK_PERIOD;
    return p < BLINK_ON_1 || (p >= BLINK_GAP && p < BLINK_ON_2);
}

int jov_sim_order_by_depth(const JovSim *s, int *out) {
    int i, j, n = 0;
    for (i = 0; i < s->n_contacts; i++) {
        if (!s->contacts[i].dead) out[n++] = i;
    }
    /* Insertion sort, far to near. n is a couple of dozen at worst and this
     * runs once a frame; the clarity is worth more than the comparisons. */
    for (i = 1; i < n; i++) {
        int v = out[i];
        for (j = i - 1; j >= 0 && s->contacts[out[j]].z < s->contacts[v].z; j--) {
            out[j + 1] = out[j];
        }
        out[j + 1] = v;
    }
    return n;
}

/* =====================================================================
 * The bot -- what the attract mode flies
 * ===================================================================== */

void jov_bot(const JovSim *s, float *ax, float *ay, int *fire) {
    const JovContact *target = 0;
    float best = 1e30f, hw, hh, mx, my;
    int i;

    *ax = 0.0f; *ay = 0.0f; *fire = 0;

    /* Nearest hostile that is inside firing range and still ahead. */
    for (i = 0; i < s->n_contacts; i++) {
        const JovContact *c = &s->contacts[i];
        if (c->kind != JOV_HOSTILE || c->dead) continue;
        if (c->z > Z_FIRE_MAX || c->z < 0.0f) continue;
        if (c->z < best) { best = c->z; target = c; }
    }
    if (!target) return;

    /* A dead band, so the ship settles instead of oscillating either side of
     * its target -- which reads as indecision on an attract screen. */
    if (target->x > s->player.x + 1.0f) *ax = 1.0f;
    else if (target->x < s->player.x - 1.0f) *ax = -1.0f;
    if (target->y > s->player.y + 1.0f) *ay = 1.0f;
    else if (target->y < s->player.y - 1.0f) *ay = -1.0f;

    jov_player_muzzle(&s->player, &mx, &my);
    jov_hit_box(target, &hw, &hh);
    if (!proj_in_box(mx, my, target->x, target->y, hw, hh)) return;

    /* Hold fire if any convoy NEARER than the target is in the line -- or
     * could DRIFT into it before the shot arrives.
     *
     * The first version checked only the line as it stood at the moment of
     * firing, which is what a naive player does, and it shot a convoy roughly
     * once every hundred seconds. A shot is not instant: it takes
     * target->z / SHOT_SPEED frames to reach the target, and a convoy drifts
     * up to AID_DRIFT per frame in the meantime. Judging a shot safe at the
     * trigger and having it be unsafe on arrival is a real property of the
     * game and not a bug -- a human takes the same risk -- but an ATTRACT
     * SCREEN that demonstrates the one thing the game punishes is a bad
     * advert, so the bot leads.
     *
     * The margin is the whole drift a convoy could manage in the flight time,
     * which is deliberately pessimistic: it assumes every convoy drifts
     * straight at the line for the entire flight. That costs a few shots the
     * bot could safely have taken, and buys a demo that never shows the
     * FRIENDLY FIRE card. */
    {
        /* Every convoy the shot could reach, not just the ones in front of the
         * target. A shot does not stop at what it was aimed at: if the target
         * moves out of the way it carries on to Z_FIRE_MAX, and a convoy
         * BEYOND the target is then squarely in its path. Checking only nearer
         * convoys shot one about once every hundred seconds -- rarely enough
         * to look like bad luck and often enough for an attract screen to
         * demonstrate the one thing the game punishes.
         *
         * The lead is the drift a convoy could manage while the shot is in
         * flight, computed to the convoy's own depth. Deliberately pessimistic
         * -- it assumes the convoy drifts straight at the line the whole way.
         * That costs the bot a few shots it could safely have taken, which is
         * the right trade for something whose job is to look exemplary. */
        for (i = 0; i < s->n_contacts; i++) {
            const JovContact *c = &s->contacts[i];
            float aw, ah, lead;
            if (c->kind != JOV_AID || c->dead) continue;
            if (c->z < 0.0f || c->z > Z_FIRE_MAX) continue;
            lead = (c->z / SHOT_SPEED) * AID_DRIFT;
            jov_hit_box(c, &aw, &ah);
            if (proj_in_box(mx, my, c->x, c->y, aw + lead, ah)) return;
        }
    }
    *fire = 1;
}

/* =====================================================================
 * Scoring policy -- web/js/main.js
 * ===================================================================== */

void jov_run_reset(JovRun *r) {
    memset(r, 0, sizeof(*r));
    r->combo = 1;
}

static void bump_combo(JovRun *r) {
    r->combo = r->combo + 1 > COMBO_MAX ? COMBO_MAX : r->combo + 1;
    r->combo_timer = COMBO_WINDOW;
}

static void break_combo(JovRun *r) {
    r->combo = 1;
    r->combo_timer = 0.0f;
}

/* A tiny integer-to-text, because sim.c must not pull in stdio and snprintf
 * would be the only thing it wanted from it. Writes "+1200" style. */
static void fmt_signed(char *out, int n, int value) {
    char digits[12];
    int len = 0, i = 0, neg = value < 0;
    unsigned int v = neg ? (unsigned int)(-value) : (unsigned int)value;
    if (v == 0) digits[len++] = '0';
    while (v > 0 && len < (int)sizeof(digits)) { digits[len++] = (char)('0' + v % 10); v /= 10; }
    if (i < n - 1) out[i++] = neg ? '-' : '+';
    while (len > 0 && i < n - 1) out[i++] = digits[--len];
    out[i] = '\0';
}

void jov_run_resolve(JovRun *run, JovSim *sim, JovCue *cue) {
    int i;
    char text[POPUP_TEXT_MAX];

    memset(cue, 0, sizeof(*cue));

    for (i = 0; i < sim->n_events; i++) {
        const JovEvent *e = &sim->events[i];
        switch (e->type) {
        case JOV_EV_HOSTILE_KILLED:
            cue->explode++;
            run->kills++;
            run->score += SCORE_HOSTILE * run->combo;
            fmt_signed(text, sizeof(text), SCORE_HOSTILE * run->combo);
            jov_sim_add_popup(sim, e->x, e->y, e->z, text, C_HUD_TEXT);
            bump_combo(run);
            break;

        case JOV_EV_AID_ESCORTED:
            cue->escort++;
            run->escorted++;
            run->score += SCORE_ESCORT;
            bump_combo(run);
            /* Drawn at a fixed shallow depth rather than the contact's own:
             * an escort is reported as the convoy leaves the frame, and a
             * popup at Z_NEAR would be behind the camera. */
            fmt_signed(text, sizeof(text) - 5, SCORE_ESCORT);
            memmove(text + 5, text, strlen(text) + 1);
            memcpy(text, "SAFE ", 5);
            jov_sim_add_popup(sim, e->x, e->y, 30.0f, text, C_AID);
            break;

        case JOV_EV_AID_LOST:
            cue->lost++;
            run->lost++;
            break_combo(run);
            cue->shake += 3.0f;
            jov_sim_add_popup(sim, e->x, e->y, e->z, "LOST", C_WARN);
            break;

        case JOV_EV_FRIENDLY_FIRE:
            cue->friendly_fire++;
            run->strikes++;
            run->score += SCORE_FRIENDLY_FIRE;
            break_combo(run);
            cue->shake += 9.0f;
            cue->flash = 0.85f;
            cue->flash_color = C_WARN;
            jov_sim_add_popup(sim, e->x, e->y, e->z, "FRIENDLY FIRE", C_WARN);
            break;

        case JOV_EV_PLAYER_HIT:
            if (jov_player_take_hit(&sim->player)) {
                cue->player_hit++;
                cue->shake += 7.0f;
                cue->flash = 0.5f;
                cue->flash_color = 0xFFFFFFFF;
                break_combo(run);
            }
            break;

        /* The transponder, heard. Carries information, so it fires even when
         * nothing else about the contact has happened yet. */
        case JOV_EV_AID_SIGHTED:
            cue->ping++;
            break;

        /* A convoy has been marked. Says so in words as well as with the link
         * and the countdown render.c draws, because this is the one rule
         * players were not deducing on their own. */
        case JOV_EV_AID_LOCKED:
            cue->lock++;
            jov_sim_add_popup(sim, e->x, e->y, e->z, "UNDER FIRE", C_WARN);
            break;

        default:
            break;
        }
    }
    sim->n_events = 0;
}

void jov_run_tick(JovRun *run, float dt) {
    if (run->combo_timer > 0.0f) {
        run->combo_timer -= dt;
        if (run->combo_timer <= 0.0f) run->combo = 1;
    }
}

int jov_run_over(const JovRun *run, const JovPlayer *p) {
    return p->lives <= 0 || run->strikes >= MAX_STRIKES;
}

/*
 * A rank from how the convoys fared, not from the score.
 *
 * Scoring already rewards escorts, but a rank that read only the number would
 * let a big kill count paper over a bad record -- and the record is the thing
 * the title is about.
 */
const char *jov_run_rank(const JovRun *run) {
    int seen = run->escorted + run->lost;
    float ratio;
    if (run->strikes >= MAX_STRIKES) return "COURT-MARTIALLED";
    if (seen == 0) return "UNTESTED";
    ratio = (float)run->escorted / (float)seen;
    if (run->strikes == 0 && ratio >= 0.95f) return "EXEMPLARY";
    if (ratio >= 0.8f)  return "COMMENDED";
    if (ratio >= 0.6f)  return "ACCEPTABLE";
    if (ratio >= 0.35f) return "CENSURED";
    return "NEGLIGENT";
}
