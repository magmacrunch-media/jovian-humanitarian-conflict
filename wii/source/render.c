/* =====================================================================
 * render.c -- The Jovian Humanitarian Conflict (Wii)
 *
 * Draws state, decides nothing. A port of the drawing halves of
 * web/js/world.js, entities.js and player.js -- the same shapes, the same
 * gradients, the same palette, built out of what GX gives us instead of what
 * the Canvas API gives the browser.
 *
 * Nothing here may change simulation state. The day the renderer nudges a
 * contact is the day `make test` stops being a statement about the game.
 *
 * ---------------------------------------------------------------------
 * How the Canvas calls map onto GX.
 *
 * `GRRLIB_NGoneFilled` takes a vertex array and a COLOUR PER VERTEX and draws
 * a triangle fan, so two things the placeholder faked are now real:
 *
 *   ctx.createLinearGradient  ->  a quad whose two ends carry different
 *                                 colours; GX interpolates across it. vquad()
 *                                 and hquad() below.
 *   ctx.moveTo/lineTo/fill    ->  tri(). The hostile silhouette and the ship's
 *                                 wings are triangles in the browser and were
 *                                 stacks of rectangles here.
 *
 * The one Canvas call with no equivalent is `ctx.clip()` to a circle, which
 * the browser uses to cut the gas giant's bands to its disc. GX's scissor is
 * rectangular. So the giant is drawn as horizontal strips chorded to the
 * circle instead -- see draw_giant(), which also folds the terminator into the
 * strip colours rather than overdrawing a shadow on top, and so does in one
 * pass what the browser needs three for.
 *
 * ---------------------------------------------------------------------
 * Two coordinate spaces, and which is which.
 *
 * The PLAYFIELD is drawn in the game's own 480x270 design frame -- the
 * browser's canvas, kept so the tuning transfers -- letterboxed into the
 * TV-safe area by rd_map_*(). The HUD is drawn in magnolia's 640x480 design
 * space through ui_draw_*(), because that is what the engine's overscan
 * handling and font metrics are written against.
 *
 * Mixing them is the mistake to watch for: a HUD coordinate passed to
 * rd_map_x() lands in roughly the right place on NTSC and in the wrong place
 * on PAL, which is the kind of thing nobody sees until somebody else runs it.
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

void rd_playfield_begin(void) {
    float x = g_ox, y = g_oy;
    float w = (float)CANVAS_W * g_scale, h = (float)CANVAS_H * g_scale;
    if (x < 0.0f) { w += x; x = 0.0f; }
    if (y < 0.0f) { h += y; y = 0.0f; }
    if (w <= 0.0f || h <= 0.0f) return;
    GRRLIB_ClipDrawing((u32)x, (u32)y, (u32)w, (u32)h);
}

void rd_playfield_end(void) { GRRLIB_ClipReset(); }

/* -- Colour ------------------------------------------------------------ */

/* 0xRRGGBBAA with the alpha scaled to a fraction of its own. */
static unsigned int fade(unsigned int rgba, float a) {
    unsigned int base = rgba & 0xFFu;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    return (rgba & 0xFFFFFF00u) | (unsigned int)((float)base * a);
}

/* Blend `over` onto `base` by `a`, in the colour channels rather than by
 * drawing one on top of the other. Used for the gas giant's terminator: the
 * browser lays a translucent gradient over the finished disc, which costs a
 * second pass and blends against whatever is behind the planet at the edges.
 * Folding it into the strip colour has neither problem. */
static unsigned int mix(unsigned int base, unsigned int over, float a) {
    unsigned int br = (base >> 24) & 0xFF, bg = (base >> 16) & 0xFF, bb = (base >> 8) & 0xFF;
    unsigned int orr = (over >> 24) & 0xFF, og = (over >> 16) & 0xFF, ob = (over >> 8) & 0xFF;
    unsigned int r, g, b;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    r = (unsigned int)((float)br + ((float)orr - (float)br) * a);
    g = (unsigned int)((float)bg + ((float)og - (float)bg) * a);
    b = (unsigned int)((float)bb + ((float)ob - (float)bb) * a);
    return (r << 24) | (g << 16) | (b << 8) | (base & 0xFFu);
}

/* -- Primitives, all in design coordinates ----------------------------- */

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

/* A flat triangle -- ctx.moveTo/lineTo/lineTo/fill. */
static void tri(float x1, float y1, float x2, float y2, float x3, float y3,
                unsigned int color) {
    guVector v[3];
    u32 c[3];
    v[0].x = rd_map_x(x1); v[0].y = rd_map_y(y1); v[0].z = 0.0f;
    v[1].x = rd_map_x(x2); v[1].y = rd_map_y(y2); v[1].z = 0.0f;
    v[2].x = rd_map_x(x3); v[2].y = rd_map_y(y3); v[2].z = 0.0f;
    c[0] = c[1] = c[2] = color;
    GRRLIB_NGoneFilled(v, c, 3);
}

/* A quad with a vertical gradient -- ctx.createLinearGradient(0,y0,0,y1). */
static void vquad(float dx, float dy, float dw, float dh,
                  unsigned int top, unsigned int bottom) {
    guVector v[4];
    u32 c[4];
    float x0 = rd_map_x(dx), x1 = rd_map_x(dx + dw);
    float y0 = rd_map_y(dy), y1 = rd_map_y(dy + dh);
    v[0].x = x0; v[0].y = y0; v[0].z = 0.0f; c[0] = top;
    v[1].x = x1; v[1].y = y0; v[1].z = 0.0f; c[1] = top;
    v[2].x = x1; v[2].y = y1; v[2].z = 0.0f; c[2] = bottom;
    v[3].x = x0; v[3].y = y1; v[3].z = 0.0f; c[3] = bottom;
    GRRLIB_NGoneFilled(v, c, 4);
}

/* A quad with a horizontal gradient. */
static void hquad(float dx, float dy, float dw, float dh,
                  unsigned int left, unsigned int right) {
    guVector v[4];
    u32 c[4];
    float x0 = rd_map_x(dx), x1 = rd_map_x(dx + dw);
    float y0 = rd_map_y(dy), y1 = rd_map_y(dy + dh);
    v[0].x = x0; v[0].y = y0; v[0].z = 0.0f; c[0] = left;
    v[1].x = x1; v[1].y = y0; v[1].z = 0.0f; c[1] = right;
    v[2].x = x1; v[2].y = y1; v[2].z = 0.0f; c[2] = right;
    v[3].x = x0; v[3].y = y1; v[3].z = 0.0f; c[3] = left;
    GRRLIB_NGoneFilled(v, c, 4);
}

/* ctx.ellipse(...).fill(). GRRLIB draws circles only, and the Spot is half as
 * tall as it is wide -- drawn as a fan so it is one primitive rather than a
 * stack of rows. */
static void ellipse(float cx, float cy, float rx, float ry, unsigned int color) {
    enum { SEG = 20 };
    guVector v[SEG + 2];
    u32 c[SEG + 2];
    int i;
    v[0].x = rd_map_x(cx); v[0].y = rd_map_y(cy); v[0].z = 0.0f; c[0] = color;
    for (i = 0; i <= SEG; i++) {
        float a = 6.28318530718f * (float)i / (float)SEG;
        v[i + 1].x = rd_map_x(cx + cosf(a) * rx);
        v[i + 1].y = rd_map_y(cy + sinf(a) * ry);
        v[i + 1].z = 0.0f;
        c[i + 1] = color;
    }
    GRRLIB_NGoneFilled(v, c, SEG + 2);
}

/* -- The rail ---------------------------------------------------------- */

/*
 * The gas giant, low and left behind the rail.
 *
 * Bands are horizontal slabs of a fixed palette rather than a gradient: at
 * this size a smooth ramp turns to mud, and Jupiter reads as Jupiter because
 * of the banding, not the colour.
 *
 * One strip per two design pixels, chorded to the circle at its own height.
 * The first version of this drew ONE rectangle per band sized to the band's
 * widest point, which is not a disc: it is a stepped layer cake, widest below
 * the equator and never closing at the bottom, and it looked exactly like one.
 *
 * The terminator -- the limb away from the rail falling into shadow -- is
 * folded into each strip's colour rather than laid over the finished disc as
 * the browser does. Same result, one pass, and no translucent rectangle
 * hanging off the edges of the planet.
 */
static void draw_giant(const JovRail *rail) {
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

    for (i = -(int)r; i <= (int)r; i += 2) {
        float fy = (float)i / r;                  /* -1 at the pole, 0 at the equator */
        float hw = r * sqrtf(1.0f - fy * fy);
        float y = cy + (float)i;
        unsigned int col = cols[7];
        unsigned int l, m, rt;
        int b;

        for (b = 0; b < 8; b++) {
            if (fy >= bands[b][0] && fy < bands[b][1]) { col = cols[b]; break; }
        }

        /* The browser's three gradient stops: 0.62 shadow at the left limb,
         * clear at 45% across, 0.30 at the right. Two quads, because a single
         * one interpolates end to end and would wash out the lit band. */
        l  = mix(col, C_VOID, 0.62f);
        m  = col;
        rt = mix(col, C_VOID, 0.30f);
        hquad(cx - hw, y, hw * 2.0f * 0.45f, 2.0f, l, m);
        hquad(cx - hw + hw * 2.0f * 0.45f, y, hw * 2.0f * 0.55f, 2.0f, m, rt);
    }

    /* The Spot, sitting in the umber band below the equator. Shaded to match
     * the terminator it sits under. */
    ellipse(cx - 22.0f, cy + 26.0f, 20.0f, 9.0f, mix(C_SPOT_RED, C_VOID, 0.22f));
    ellipse(cx - 22.0f, cy + 26.0f, 11.0f, 4.5f, mix(C_SPOT_CORE, C_VOID, 0.22f));
}

/*
 * The cloud deck: the rail's floor, drawn as bands streaming toward the
 * camera. Each band's screen y and width come straight from the projection, so
 * a band's motion up the screen is the same hyperbolic curve the ships follow.
 * Drawn far to near so nearer bands overlap the haze behind them.
 */
static void draw_deck(const JovRail *rail) {
    const float DECK_Y = 150.0f;   /* world y of the deck surface, below the ship */
    const float HALF = 900.0f;     /* world half-width of a band */
    int order[RAIL_BANDS];
    int i, j;
    ProjPoint v;

    /* Deck ground, horizon to the bottom of the frame. */
    vquad(0.0f, HORIZON_Y, (float)CANVAS_W, (float)CANVAS_H - HORIZON_Y,
          C_DECK_FAR, C_DECK_NEAR);

    for (i = 0; i < RAIL_BANDS; i++) order[i] = i;
    for (i = 1; i < RAIL_BANDS; i++) {
        int k = order[i];
        for (j = i - 1; j >= 0 && rail->bands[order[j]] < rail->bands[k]; j--) {
            order[j + 1] = order[j];
        }
        order[j + 1] = k;
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
        ProjPoint n = proj_point((float)i * 210.0f, DECK_Y, 20.0f,
                                 rail->cam_x, rail->cam_y);
        line(v.x, v.y, n.x, n.y, fade(C_DECK_LINE, 0.30f));
    }
}

void rd_draw_rail(const JovRail *rail) {
    int i;

    /* Void above the horizon, dark at the top and hazing toward the deck. */
    vquad(0.0f, 0.0f, (float)CANVAS_W, HORIZON_Y, C_VOID, C_VOID_HAZE);

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
         * Z_SHAPE_READABLE inward, well before it can be shot. Deliberately
         * rectangular: the silhouette channel works by being the opposite
         * shape to the hostile's delta, so squaring it off is the point. */
        box(p.x - w / 2.0f, p.y - h / 2.0f, w, h, C_AID_DARK);
        box(p.x - w / 2.0f, p.y - h / 2.0f, w, fmaxf(1.0f, h * 0.55f), C_AID);
        /* The cross bar. Kept at least a pixel so it survives the far
         * distances -- at spawn depth this is the only part still resolvable. */
        box(p.x - w * 0.09f, p.y - h / 2.0f, fmaxf(1.0f, w * 0.18f), h, C_AID_PALE);
        box(p.x - w / 2.0f, p.y - h * 0.09f, w, fmaxf(1.0f, h * 0.18f), C_AID_PALE);

        /* A convoy under fire gets its own alarm, separate from the beacon:
         * this is the thing you are being asked to save. */
        if (c->doom_timer > 0.0f && ((int)(sim->frame / 4.0f)) % 2 == 0) {
            frame_box(p.x - w / 2.0f - 3.0f, p.y - h / 2.0f - 3.0f,
                      w + 6.0f, h + 6.0f, C_WARN);
        }
    } else {
        /* Angular delta, nose toward the camera. A real triangle now: the
         * placeholder stacked four rectangles, which read as a staircase at
         * the near distances where the silhouette is supposed to be doing its
         * job. */
        tri(p.x, p.y + h / 2.0f,
            p.x - w / 2.0f, p.y - h / 2.0f,
            p.x + w / 2.0f, p.y - h / 2.0f, C_HOSTILE_DARK);
        tri(p.x, p.y + h * 0.28f,
            p.x - w * 0.32f, p.y - h / 2.0f,
            p.x + w * 0.32f, p.y - h / 2.0f, C_HOSTILE);
        if (p.s > 0.4f) box(p.x - 1.0f, p.y - h * 0.2f, 2.0f, 2.0f, C_HOSTILE_EYE);
    }

    /* -- The transponder --
     * Drawn at a CONSTANT design size regardless of depth. That is the whole
     * point: it is the one channel that does not shrink into illegibility, so
     * a convoy is identifiable on the frame it appears. Halo first, core over
     * it -- the other order washes the bright mark out under its own halo. */
    if (jov_beacon_lit(c, sim->frame)) {
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

    /* Popups. These were the frames that used to blow the budget, back when
     * every glyph was a FreeType load; magnolia caches them now and a popup
     * costs about as much as the explosion under it. */
    for (i = 0; i < sim->n_popups; i++) {
        const JovPopup *p = &sim->popups[i];
        ProjPoint s = proj_point(p->x, p->y, p->z, sim->rail.cam_x, sim->rail.cam_y);
        float a = p->life / (p->max * 0.5f);
        u32 tw;
        if (a > 1.0f) a = 1.0f;
        tw = text_width(p->text, 12);
        text_draw((int)(rd_map_x(s.x) - (float)tw * 0.5f), (int)rd_map_y(s.y),
                  p->text, 12, fade(p->color, a));
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

    /* Wings, as the browser draws them: two triangles whose outboard tip
     * rises or falls with the bank. A cheap fake roll that reads correctly at
     * this size and costs no transform -- and the reason it has to be a
     * triangle is that the tip is the only part that moves. */
    tri(sp.x - w / 2.0f, sp.y + 2.0f + b * 5.0f,
        sp.x - 5.0f,     sp.y - 2.0f,
        sp.x - 5.0f,     sp.y + 5.0f, C_SHIP_STEEL);
    tri(sp.x + w / 2.0f, sp.y + 2.0f - b * 5.0f,
        sp.x + 5.0f,     sp.y - 2.0f,
        sp.x + 5.0f,     sp.y + 5.0f, C_SHIP_STEEL);

    /* Hull: a nose-up triangle, not the rectangle the placeholder used. */
    tri(sp.x,        sp.y - h / 2.0f - 3.0f,
        sp.x + 5.0f, sp.y + 4.0f,
        sp.x - 5.0f, sp.y + 4.0f, C_SHIP_HULL);

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
        int x = 116 + (int)((c->x + SPAWN_X_RANGE) / (SPAWN_X_RANGE * 2.0f) * 484.0f);
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

void rd_draw_title_text(void) {
    ui_draw_centered_text(150, "THE JOVIAN", 30, C_AID);
    ui_draw_centered_text(190, "HUMANITARIAN CONFLICT", 22, C_AID);
    ui_draw_centered_text(250, "KNOW WHAT YOU ARE SHOOTING AT", 12, C_HUD_DIM);
    ui_draw_centered_text(320, "AID CONVOYS SQUAWK A DOUBLE BLINK", 12, C_HUD_TEXT);
    ui_draw_centered_text(342, "SHOOT ONE AND IT COSTS YOU THE RUN", 12, C_WARN);
    ui_draw_centered_text(400, "PRESS A", 16, C_SHIP_GLASS);
}

/* A convoy and a hostile, drawn at the size and shape they appear at in play,
 * with the transponder blinking on the convoy exactly as it will in the game.
 * The blink is the point: it is channel one, and this is the only place the
 * player is shown it without also being shot at. */
void rd_draw_ready(float frame) {
    JovContact aid, hostile;
    JovSim stub;

    memset(&stub, 0, sizeof(stub));
    stub.frame = frame;

    memset(&aid, 0, sizeof(aid));
    aid.kind = JOV_AID;
    aid.x = -70.0f; aid.y = -6.0f; aid.z = 60.0f;

    memset(&hostile, 0, sizeof(hostile));
    hostile.kind = JOV_HOSTILE;
    hostile.x = 70.0f; hostile.y = -6.0f; hostile.z = 60.0f;

    ui_draw_dim_overlay(fade(C_VOID, 0.72f));
    ui_draw_centered_text(60, "KNOW WHAT YOU ARE SHOOTING AT", 16, C_HUD_TEXT);

    rd_playfield_begin();
    draw_contact(&stub, &aid);
    draw_contact(&stub, &hostile);
    rd_playfield_end();

    ui_draw_text_centered_in(60, 250, 260, 30, "AID CONVOY", 14, C_AID);
    ui_draw_text_centered_in(60, 272, 260, 30, "DOUBLE BLINK - HOLD FIRE", 10, C_HUD_DIM);
    ui_draw_text_centered_in(320, 250, 260, 30, "HOSTILE", 14, C_HOSTILE);
    ui_draw_text_centered_in(320, 272, 260, 30, "DARK - SHOOT IT", 10, C_HUD_DIM);

    ui_draw_centered_text(330, "D-PAD FLIES    1 OR 2 FIRES    + PAUSES", 12, C_HUD_TEXT);
    ui_draw_centered_text(356, "SHOOT A CONVOY AND IT COSTS YOU THE RUN", 12, C_WARN);
    ui_draw_centered_text(410, "PRESS A", 16, C_SHIP_GLASS);
}

void rd_draw_paused(void) {
    ui_draw_dim_overlay(fade(C_VOID, 0.70f));
    ui_draw_centered_text(210, "PAUSED", 30, C_HUD_TEXT);
    ui_draw_centered_text(260, "+ TO RESUME    HOME TO QUIT", 12, C_HUD_DIM);
}

/*
 * The initials editor.
 *
 * magnolia owns the editing -- left and right change the letter, up sets it,
 * down sets it and advances, A commits -- so this only has to make the state
 * legible. The one thing it must get right is which slot is being edited: the
 * cursor is the only feedback the player has that up and down did anything.
 */
void rd_draw_initials(const GameStateMachine *gs, const JovRun *run) {
    char buf[48];
    int i;
    const int SLOT_W = 60;
    const int X0 = 320 - (SLOT_W * 3) / 2;

    ui_draw_dim_overlay(fade(C_VOID, 0.85f));

    snprintf(buf, sizeof(buf), "RANK %d", gs->rank);
    ui_draw_centered_text(90, buf, 22, C_AID);
    snprintf(buf, sizeof(buf), "%d", run->score);
    ui_draw_centered_text(130, buf, 26, C_HUD_TEXT);

    for (i = 0; i < 3; i++) {
        int x = X0 + i * SLOT_W;
        int editing = (i == gs->cursor_pos);
        /* The letter under the cursor comes from selected_letter, not from the
         * string: the shell only writes it into initials[] when up or down is
         * pressed, so drawing the string would show the previous letter while
         * the player is scrolling through them. */
        char ch[2];
        ch[0] = editing ? (char)('A' + gs->selected_letter) : gs->initials[i];
        ch[1] = '\0';

        ui_draw_panel(x + 6, 200, SLOT_W - 12, 56,
                      editing ? 0x1A2340FF : 0x10152AFF,
                      editing ? C_SHIP_GLASS : C_HUD_DIM, 3);
        ui_draw_text_centered_in(x + 6, 200, SLOT_W - 12, 56, ch, 26,
                                 editing ? C_HUD_TEXT : C_HUD_DIM);
    }

    ui_draw_centered_text(280, "LEFT/RIGHT PICK    DOWN NEXT    A DONE", 11, C_HUD_DIM);

    if (!scoring_persisted()) {
        /* magnolia probes the card with a real write at startup, so this is
         * known before a run is even played. Saying it here rather than
         * silently failing is the whole reason that probe exists. */
        ui_draw_centered_text(330, "NO SD CARD - THIS SCORE WILL NOT BE KEPT", 11, C_WARN);
    }
}

/*
 * The table. Ten rows at most, and the row just entered is picked out -- after
 * typing three letters the first thing anybody looks for is their own line.
 */
void rd_draw_scores(const GameStateMachine *gs) {
    char buf[48];
    int n = scoring_get_count();
    int i;

    ui_draw_dim_overlay(fade(C_VOID, 0.88f));
    ui_draw_centered_text(50, "BEST RUNS", 22, C_AID);

    if (n <= 0) {
        ui_draw_centered_text(200, "NO RUNS RECORDED", 14, C_HUD_DIM);
    }

    for (i = 0; i < n && i < 10; i++) {
        const ScoreEntry *e = scoring_get_entry(i);
        int y = 100 + i * 30;
        /* gs->rank is 1-based and set when the run qualified; 0 means it did
         * not, in which case nothing is highlighted. */
        int mine = (gs->rank > 0 && gs->rank == i + 1);
        u32 col = mine ? C_AID : C_HUD_TEXT;
        if (!e) continue;

        if (mine) ui_draw_panel(120, y - 4, 400, 28, 0x1A2340FF, C_AID, 3);

        snprintf(buf, sizeof(buf), "%2d", i + 1);
        ui_draw_text_shadow(140, y, buf, 14, mine ? C_AID : C_HUD_DIM);
        ui_draw_text_shadow(200, y, e->initials, 14, col);
        snprintf(buf, sizeof(buf), "%d", e->score);
        ui_draw_text_shadow(300, y, buf, 14, col);
    }

    if (!scoring_persisted()) {
        ui_draw_centered_text(410, "NO SD CARD - NOTHING IS BEING SAVED", 11, C_WARN);
    }
    ui_draw_centered_text(440, "PRESS A", 14, C_SHIP_GLASS);
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
