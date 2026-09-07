/* =====================================================================
 * render.c -- The Jovian Humanitarian Conflict (Wii)
 *
 * PLACEHOLDER, and honestly so. See render.h.
 *
 * Everything is rectangles, lines and circles: no sprite has been drawn for
 * this game yet, and a placeholder built out of primitives is one that cannot
 * quietly become the shipped look by nobody getting round to replacing it.
 *
 * The one thing here that is NOT a placeholder is the transponder. It is the
 * channel the fairness argument rests on, so it is drawn at a constant design
 * size at every depth, exactly as web/js/entities.js draws it, and the marker
 * is sized in design units rather than screen pixels so a TV does not shrink
 * it to a speck. Replace the hulls freely; leave that alone.
 * ===================================================================== */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <magnolia.h>

#include "render.h"

/* -- The letterbox ---------------------------------------------------- */

static float g_ox, g_oy, g_scale;
static float g_shake, g_flash;
static unsigned int g_flash_color;

#define STAR_COUNT 46
static struct { float x, y, b; } g_stars[STAR_COUNT];

void rd_init(void) {
    float sw = (float)ui_safe_w();
    float sh = (float)ui_safe_h();
    float fit_x = sw / (float)CANVAS_W;
    float fit_y = sh / (float)CANVAS_H;
    g_scale = fit_x < fit_y ? fit_x : fit_y;
    g_ox = (float)ui_safe_x() + (sw - (float)CANVAS_W * g_scale) * 0.5f;
    g_oy = (float)ui_safe_y() + (sh - (float)CANVAS_H * g_scale) * 0.5f;
}

float rd_map_x(float design_x) { return g_ox + design_x * g_scale + g_shake * 0.6f; }
float rd_map_y(float design_y) { return g_oy + design_y * g_scale; }
float rd_map_s(float u)        { return u * g_scale; }

void rd_add_shake(float amount) { g_shake += amount; if (g_shake > 12.0f) g_shake = 12.0f; }

void rd_add_flash(float alpha, unsigned int color) {
    if (alpha > g_flash) { g_flash = alpha; g_flash_color = color; }
}

void rd_decay(float dt) {
    /* Shake alternates sign as it decays, which is what makes it read as a
     * shake rather than as the picture sliding off. */
    g_shake *= powf(0.86f, dt);
    g_shake = -g_shake;
    if (fabsf(g_shake) < 0.05f) g_shake = 0.0f;
    g_flash *= powf(0.80f, dt);
    if (g_flash < 0.01f) g_flash = 0.0f;
}

void rd_reset_stars(unsigned int seed) {
    JovRng r;
    int i;
    jov_rng_seed(&r, seed);
    for (i = 0; i < STAR_COUNT; i++) {
        g_stars[i].x = jov_rng_next(&r) * (float)CANVAS_W;
        g_stars[i].y = jov_rng_next(&r) * HORIZON_Y;
        g_stars[i].b = 0.25f + jov_rng_next(&r) * 0.6f;
    }
}

/* -- Colour helpers ---------------------------------------------------- */

/* 0xRRGGBBAA with the alpha replaced by a 0..1 fraction of its own. */
static unsigned int fade(unsigned int rgba, float a) {
    unsigned int base = rgba & 0xFFu;
    unsigned int alpha;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    alpha = (unsigned int)((float)base * a);
    return (rgba & 0xFFFFFF00u) | alpha;
}

static void box(float dx, float dy, float dw, float dh, unsigned int color) {
    GRRLIB_Rectangle(rd_map_x(dx), rd_map_y(dy), rd_map_s(dw), rd_map_s(dh),
                     color, true);
}

static void frame_box(float dx, float dy, float dw, float dh, unsigned int color) {
    GRRLIB_Rectangle(rd_map_x(dx), rd_map_y(dy), rd_map_s(dw), rd_map_s(dh),
                     color, false);
}

static void line(float x1, float y1, float x2, float y2, unsigned int color) {
    GRRLIB_Line(rd_map_x(x1), rd_map_y(y1), rd_map_x(x2), rd_map_y(y2), color);
}

/* -- The rail ---------------------------------------------------------- */

static void draw_giant(const JovRail *rail) {
    /* Bands are horizontal slabs of a fixed palette rather than a gradient: at
     * this size a smooth ramp turns to mud, and Jupiter reads as Jupiter
     * because of the banding, not the colour.
     *
     * GRRLIB has no clip region, so the disc is approximated by chording each
     * band to the circle rather than by clipping a rectangle to it. The
     * arithmetic is the same either way and this needs no state. */
    static const float bands[8][2] = {
        { -1.00f, -0.72f }, { -0.72f, -0.50f }, { -0.50f, -0.30f },
        { -0.30f, -0.10f }, { -0.10f,  0.14f }, {  0.14f,  0.36f },
        {  0.36f,  0.58f }, {  0.58f,  1.00f }
    };
    static const unsigned int cols[8] = {
        C_BAND_SHADOW, C_BAND_TAN, C_BAND_CREAM, C_BAND_UMBER,
        C_BAND_TAN, C_BAND_CREAM, C_BAND_UMBER, C_BAND_DEEP
    };
    const float cx = 96.0f - rail->cam_x * 0.12f;
    const float cy = HORIZON_Y - 6.0f;
    const float r = 78.0f;
    int i;

    /* One strip per two design pixels, each chorded to the circle at its own
     * height and coloured by whichever band it falls in.
     *
     * The first version of this drew ONE rectangle per band, sized to the
     * band's widest point. That is not a disc: it is a stepped layer cake,
     * widest below the equator and never closing at the bottom, and it looked
     * exactly like one on a TV. Jupiter reads as Jupiter because of the
     * banding, but only if the banding is on a sphere. */
    for (i = -(int)r; i <= (int)r; i += 2) {
        float fy = (float)i / r;                   /* -1 at the pole, 0 at the equator */
        float hw = r * sqrtf(1.0f - fy * fy);
        int b;
        unsigned int col = cols[7];
        for (b = 0; b < 8; b++) {
            if (fy >= bands[b][0] && fy < bands[b][1]) { col = cols[b]; break; }
        }
        box(cx - hw, cy + (float)i, hw * 2.0f, 2.0f, col);
    }

    /* The Spot, sitting in the umber band below the equator. */
    GRRLIB_Circle(rd_map_x(cx - 22.0f), rd_map_y(cy + 26.0f), rd_map_s(12.0f),
                  C_SPOT_RED, true);
    GRRLIB_Circle(rd_map_x(cx - 22.0f), rd_map_y(cy + 26.0f), rd_map_s(6.0f),
                  C_SPOT_CORE, true);
}

static void draw_deck(const JovRail *rail) {
    const float DECK_Y = 150.0f;   /* world y of the deck surface, below the ship */
    const float HALF = 900.0f;     /* world half-width of a band */
    int order[RAIL_BANDS];
    int i, j;
    ProjPoint v;

    /* Deck ground, horizon to the bottom of the frame. Two flat slabs stand in
     * for the browser's gradient. */
    box(0.0f, HORIZON_Y, (float)CANVAS_W, ((float)CANVAS_H - HORIZON_Y) * 0.5f,
        C_DECK_FAR);
    box(0.0f, HORIZON_Y + ((float)CANVAS_H - HORIZON_Y) * 0.5f, (float)CANVAS_W,
        ((float)CANVAS_H - HORIZON_Y) * 0.5f, C_DECK_NEAR);

    /* Far to near, so nearer bands overlap the haze behind them. */
    for (i = 0; i < RAIL_BANDS; i++) order[i] = i;
    for (i = 1; i < RAIL_BANDS; i++) {
        int v2 = order[i];
        for (j = i - 1; j >= 0 && rail->bands[order[j]] < rail->bands[v2]; j--) {
            order[j + 1] = order[j];
        }
        order[j + 1] = v2;
    }

    for (i = 0; i < RAIL_BANDS; i++) {
        float z = rail->bands[order[i]];
        ProjPoint l = proj_point(-HALF, DECK_Y, z, rail->cam_x, rail->cam_y);
        ProjPoint r = proj_point(HALF, DECK_Y, z, rail->cam_x, rail->cam_y);
        float t, h;
        if (l.y < HORIZON_Y || l.y > (float)CANVAS_H) continue;
        /* Near bands are brighter and thicker; far ones fade into the haze. */
        t = 1.0f - z / DECK_Z_SPAN;
        h = 1.0f + t * 2.0f;
        box(l.x, l.y, r.x - l.x, h, fade(C_DECK_LINE, 0.10f + t * 0.42f));
    }

    /* Two rails converging on the vanishing point, which is what makes the
     * camera's lateral drift legible -- without them, sliding left and sliding
     * the whole world right look identical. */
    v = proj_vanishing(rail->cam_x, rail->cam_y);
    for (i = -1; i <= 1; i += 2) {
        ProjPoint near_p = proj_point((float)i * 210.0f, DECK_Y, 20.0f,
                                      rail->cam_x, rail->cam_y);
        line(v.x, v.y, near_p.x, near_p.y, fade(C_DECK_LINE, 0.30f));
    }
}

void rd_draw_rail(const JovRail *rail) {
    int i;

    /* Void above the horizon. */
    box(0.0f, 0.0f, (float)CANVAS_W, HORIZON_Y * 0.5f, C_VOID);
    box(0.0f, HORIZON_Y * 0.5f, (float)CANVAS_W, HORIZON_Y * 0.5f, C_VOID_HAZE);

    /* Stars shift a little against the camera so the void is not a decal. */
    for (i = 0; i < STAR_COUNT; i++) {
        float x = g_stars[i].x - rail->cam_x * 0.06f;
        while (x < 0.0f) x += (float)CANVAS_W;
        while (x >= (float)CANVAS_W) x -= (float)CANVAS_W;
        box(x, g_stars[i].y, 1.0f, 1.0f, fade(C_STAR, g_stars[i].b));
    }

    draw_giant(rail);
    draw_deck(rail);
}

/* -- Contacts ---------------------------------------------------------- */

static void draw_contact(const JovSim *sim, const JovContact *c) {
    ProjPoint p = proj_point(c->x, c->y, c->z, sim->rail.cam_x, sim->rail.cam_y);
    int is_aid = c->kind == JOV_AID;
    float w, h;

    if (p.s <= 0.0f) return;
    w = (is_aid ? AID_W : HOSTILE_W) * p.s;
    h = (is_aid ? AID_H : HOSTILE_H) * p.s;

    if (is_aid) {
        /* Blunt slab with a bar across it -- legible as "not a fighter" from
         * Z_SHAPE_READABLE inward, well before it can be shot. */
        box(p.x - w / 2.0f, p.y - h / 2.0f, w, h, C_AID_DARK);
        box(p.x - w / 2.0f, p.y - h / 2.0f, w, fmaxf(1.0f, h * 0.55f), C_AID);
        /* The cross bar. Kept at least a pixel so it survives the far
         * distances. */
        box(p.x - w * 0.09f, p.y - h / 2.0f, fmaxf(1.0f, w * 0.18f), h, C_AID_PALE);
        box(p.x - w / 2.0f, p.y - h * 0.09f, w, fmaxf(1.0f, h * 0.18f), C_AID_PALE);

        /* A convoy under fire gets its own alarm, separate from the beacon:
         * this is the thing you are being asked to save. */
        if (c->doom_timer > 0.0f && ((int)(sim->frame / 4.0f)) % 2 == 0) {
            frame_box(p.x - w / 2.0f - 3.0f, p.y - h / 2.0f - 3.0f,
                      w + 6.0f, h + 6.0f, C_WARN);
        }
    } else {
        /* Angular delta, nose toward the camera. Four stacked slabs stand in
         * for the browser's triangle -- crude, and still unmistakably not the
         * convoy's slab, which is the only job the silhouette channel has. */
        int i;
        for (i = 0; i < 4; i++) {
            float f = (float)i / 4.0f;
            float bw = w * (1.0f - f * 0.75f);
            box(p.x - bw / 2.0f, p.y - h / 2.0f + h * f * 0.98f, bw, h * 0.26f,
                i == 0 ? C_HOSTILE_DARK : C_HOSTILE);
        }
        if (p.s > 0.4f) box(p.x - 1.0f, p.y - h * 0.2f, 2.0f, 2.0f, C_HOSTILE_EYE);
    }

    /* -- The transponder --
     * Drawn at a CONSTANT design size regardless of depth. That is the whole
     * point: it is the one channel that does not shrink into illegibility, so
     * a convoy is identifiable on the frame it appears. */
    if (jov_beacon_lit(c, sim->frame)) {
        /* Halo first, core over it. The other order washes the bright mark out
         * under its own translucent halo, which is exactly the channel this is
         * not allowed to weaken. */
        box(p.x - 4.0f, p.y - h / 2.0f - 8.0f, 8.0f, 8.0f, fade(C_AID_BEACON, 0.30f));
        box(p.x - 2.0f, p.y - h / 2.0f - 6.0f, 4.0f, 4.0f, C_AID_BEACON);
    }
}

/*
 * A convoy under attack, and the hostile doing it.
 *
 * Without this the escort rule was unplayable rather than merely hard. A
 * flashing box appeared round a convoy and there was nothing to say what it
 * meant, which of the several hostiles on screen had caused it, or how long was
 * left. So: a line from the convoy to its attacker, a ring round the attacker,
 * and a bar that empties as the convoy runs out of time. Between them they
 * answer what, who and how long.
 */
static void draw_locks(const JovSim *sim) {
    int i;
    for (i = 0; i < sim->n_contacts; i++) {
        const JovContact *c = &sim->contacts[i];
        const JovContact *a;
        ProjPoint v, ap;
        float pulse, ar, frac, w, bx, by;

        if (c->kind != JOV_AID || c->doom_timer <= 0.0f) continue;
        a = jov_sim_find(sim, c->locked_by);
        if (!a) continue;

        v  = proj_point(c->x, c->y, c->z, sim->rail.cam_x, sim->rail.cam_y);
        ap = proj_point(a->x, a->y, a->z, sim->rail.cam_x, sim->rail.cam_y);

        /* Pulsed rather than solid so it reads as an alarm and does not compete
         * with the rails behind it. */
        pulse = 0.45f + 0.35f * sinf(sim->frame * 0.35f);
        line(v.x, v.y, ap.x, ap.y, fade(C_WARN, pulse));

        /* A ring on the attacker: this is the one to shoot. */
        ar = HOSTILE_W * ap.s * 0.8f;
        if (ar < 5.0f) ar = 5.0f;
        GRRLIB_Circle(rd_map_x(ap.x), rd_map_y(ap.y), rd_map_s(ar),
                      fade(C_WARN, pulse), false);

        /* How long the convoy has. Empties left to right. */
        frac = c->doom_timer / AID_KILL_FRAMES;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        w = AID_W * v.s;
        if (w < 8.0f) w = 8.0f;
        bx = v.x - w / 2.0f;
        by = v.y + (AID_H * v.s) / 2.0f + 4.0f;
        box(bx, by, w, 3.0f, fade(C_VOID, 0.70f));
        box(bx, by, w * frac, 3.0f, C_WARN);
    }
}

void rd_draw_contacts(const JovSim *sim) {
    int order[MAX_CONTACTS];
    int n = jov_sim_order_by_depth(sim, order);
    int i;

    for (i = 0; i < n; i++) draw_contact(sim, &sim->contacts[order[i]]);

    /* Drawn over the contacts, because it is the most urgent thing on screen
     * and must not end up behind a hull that happens to be nearer. */
    draw_locks(sim);

    for (i = 0; i < sim->n_shots; i++) {
        const JovShot *s = &sim->shots[i];
        ProjPoint p = proj_point(s->x, s->y, s->z, sim->rail.cam_x, sim->rail.cam_y);
        float r = 3.0f * p.s;
        if (r < 1.0f) r = 1.0f;
        box(p.x - r, p.y - r * 2.0f, r * 2.0f, r * 4.0f, C_SHOT);
        box(p.x - 1.0f, p.y - r, 2.0f, r * 2.0f, C_SHOT_CORE);
    }

    for (i = 0; i < sim->n_particles; i++) {
        const JovParticle *p = &sim->particles[i];
        ProjPoint s = proj_point(p->x, p->y, p->z, sim->rail.cam_x, sim->rail.cam_y);
        float size = 2.0f * s.s;
        if (size < 1.0f) size = 1.0f;
        box(s.x, s.y, size, size, fade(p->color, p->life / p->max));
    }

    for (i = 0; i < sim->n_popups; i++) {
        const JovPopup *p = &sim->popups[i];
        ProjPoint s = proj_point(p->x, p->y, p->z, sim->rail.cam_x, sim->rail.cam_y);
        float a = p->life / (p->max * 0.5f);
        u32 tw;
        if (a > 1.0f) a = 1.0f;
        if (!ttf_font) continue;
        tw = GRRLIB_WidthTTF(ttf_font, p->text, 12);
        GRRLIB_PrintfTTF((int)(rd_map_x(s.x) - (float)tw * 0.5f), (int)rd_map_y(s.y),
                         ttf_font, p->text, 12, fade(p->color, a));
    }
}

/* -- The ship ---------------------------------------------------------- */

void rd_draw_player(const JovSim *sim) {
    const JovPlayer *p = &sim->player;
    ProjPoint sp;
    float w = SHIP_W, h = SHIP_H, b = p->bank, flick, flick2;

    /* The reticle: a marker out at firing depth showing where a shot will
     * arrive. Drawn through the projection so it sits where the shot actually
     * goes, which is not directly above the ship once the camera has drifted. */
    {
        ProjPoint r = proj_point(p->x, p->y, Z_FIRE_MAX * 0.55f,
                                 sim->rail.cam_x, sim->rail.cam_y);
        unsigned int col = fade(C_SHOT, 0.5f);
        line(r.x - 5.0f, r.y, r.x - 2.0f, r.y, col);
        line(r.x + 2.0f, r.y, r.x + 5.0f, r.y, col);
        line(r.x, r.y - 5.0f, r.x, r.y - 2.0f, col);
        line(r.x, r.y + 2.0f, r.x, r.y + 5.0f, col);
    }

    /* Blink through the invincibility window, but on a slow enough cycle to
     * stay readable -- a fast flicker on a small sprite just looks like a
     * rendering fault. */
    if (p->invincible > 0.0f && ((int)(p->invincible / 5.0f)) % 2 == 0) return;

    sp = proj_point(p->x, p->y, 0.0f, sim->rail.cam_x, sim->rail.cam_y);

    /* Thrust, drawn first so the hull sits over it. Two plumes, flickering out
     * of phase so the flame does not pulse as one block. */
    flick  = 1.0f + sinf(p->thrust_phase * 0.8f) * 0.25f;
    flick2 = 1.0f + sinf(p->thrust_phase * 0.8f + 2.0f) * 0.25f;
    box(sp.x - 6.0f, sp.y + h * 0.5f, 4.0f, 7.0f * flick, C_THRUST);
    box(sp.x + 2.0f, sp.y + h * 0.5f, 4.0f, 7.0f * flick2, C_THRUST);
    box(sp.x - 5.0f, sp.y + h * 0.5f, 2.0f, 4.0f * flick, C_THRUST_HOT);
    box(sp.x + 3.0f, sp.y + h * 0.5f, 2.0f, 4.0f * flick2, C_THRUST_HOT);

    /* Wings. Banking lifts one tip and drops the other -- a cheap fake roll
     * that reads correctly at this size and costs no transform. */
    box(sp.x - w / 2.0f, sp.y - 2.0f + b * 5.0f, w / 2.0f - 5.0f, 4.0f, C_SHIP_STEEL);
    box(sp.x + 5.0f,     sp.y - 2.0f - b * 5.0f, w / 2.0f - 5.0f, 4.0f, C_SHIP_STEEL);

    /* Hull, shadow, canopy. */
    box(sp.x - 5.0f, sp.y - h / 2.0f - 3.0f, 10.0f, h / 2.0f + 7.0f, C_SHIP_HULL);
    box(sp.x - 5.0f, sp.y + 4.0f, 10.0f, 2.0f, C_SHIP_SHADOW);
    box(sp.x - 2.0f, sp.y - 4.0f, 4.0f, 4.0f, C_SHIP_GLASS);
}

/* -- HUD --------------------------------------------------------------
 * magnolia's 640x480 design space from here down, not the playfield's.
 */

void rd_draw_hud(const JovSim *sim, const JovRun *run) {
    char buf[48];
    int i, n, order[MAX_CONTACTS];

    snprintf(buf, sizeof(buf), "%d", run->score);
    ui_draw_text_shadow(16, 12, buf, 20, C_HUD_TEXT);

    snprintf(buf, sizeof(buf), "x%d", run->combo);
    ui_draw_text_shadow(16, 40, buf, 14,
                        run->combo > 1 ? C_LIFE_ICON : C_HUD_DIM);

    /* Ships left, then strikes. Strikes are drawn lit-to-dim rather than as a
     * number because three is the whole budget and a bar is read faster than a
     * digit under fire. */
    for (i = 0; i < MAX_LIVES; i++) {
        GRRLIB_Rectangle((f32)ui_map_x(430 + i * 18), (f32)ui_map_y(14),
                         (f32)ui_map_w(12), (f32)ui_map_h(8),
                         i < sim->player.lives ? C_LIFE_ICON : C_HUD_DIM, true);
    }
    for (i = 0; i < MAX_STRIKES; i++) {
        GRRLIB_Rectangle((f32)ui_map_x(430 + i * 18), (f32)ui_map_y(32),
                         (f32)ui_map_w(12), (f32)ui_map_h(8),
                         i < run->strikes ? C_STRIKE : C_HUD_DIM, true);
    }

    /* The contact strip -- identification channel 2, so a cluttered frame never
     * hides the answer. Nearest last, and the convoys are the ones that matter,
     * so they get the wider tick. */
    n = jov_sim_order_by_depth(sim, order);
    for (i = 0; i < n; i++) {
        const JovContact *c = &sim->contacts[order[i]];
        int is_aid = c->kind == JOV_AID;
        int x = 40 + (int)((c->x + SPAWN_X_RANGE) / (SPAWN_X_RANGE * 2.0f) * 560.0f);
        GRRLIB_Rectangle((f32)ui_map_x(x), (f32)ui_map_y(452),
                         (f32)ui_map_w(is_aid ? 10 : 5), (f32)ui_map_h(6),
                         is_aid ? C_AID : C_HOSTILE, true);
    }
    ui_draw_text_shadow(16, 446, "CONTACTS", 10, C_HUD_DIM);

    /* The flash sits over everything except the HUD text it would make
     * illegible, which is why it is here rather than at the end of the frame. */
    if (g_flash > 0.0f) ui_draw_dim_overlay(fade(g_flash_color, g_flash * 0.6f));
}

/* -- Cards ------------------------------------------------------------- */

void rd_draw_title(const JovRail *rail, float frame) {
    rd_draw_rail(rail);
    (void)frame;
    ui_draw_centered_text(150, "THE JOVIAN", 30, C_AID);
    ui_draw_centered_text(190, "HUMANITARIAN CONFLICT", 22, C_AID);
    ui_draw_centered_text(250, "KNOW WHAT YOU ARE SHOOTING AT", 12, C_HUD_DIM);
    ui_draw_centered_text(320, "AID CONVOYS SQUAWK A DOUBLE BLINK", 12, C_HUD_TEXT);
    ui_draw_centered_text(342, "SHOOT ONE AND IT COSTS YOU THE RUN", 12, C_WARN);
    ui_draw_centered_text(400, "PRESS A", 16, C_SHIP_GLASS);
}

void rd_draw_results(const JovRun *run) {
    char buf[64];
    ui_draw_dim_overlay(fade(C_VOID, 0.80f));
    ui_draw_centered_text(120, jov_run_rank(run), 26,
                          run->strikes >= MAX_STRIKES ? C_WARN : C_AID);
    snprintf(buf, sizeof(buf), "%d", run->score);
    ui_draw_centered_text(170, buf, 30, C_HUD_TEXT);
    snprintf(buf, sizeof(buf), "ESCORTED %d   LOST %d", run->escorted, run->lost);
    ui_draw_centered_text(230, buf, 14, C_HUD_TEXT);
    snprintf(buf, sizeof(buf), "KILLS %d   STRIKES %d", run->kills, run->strikes);
    ui_draw_centered_text(256, buf, 14, C_HUD_DIM);
    ui_draw_centered_text(360, "PRESS A TO FLY AGAIN", 14, C_SHIP_GLASS);
    ui_draw_centered_text(384, "HOME TO QUIT", 12, C_HUD_DIM);
}
