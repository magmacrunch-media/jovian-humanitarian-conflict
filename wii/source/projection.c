/* =====================================================================
 * projection.c -- The Jovian Humanitarian Conflict (Wii)
 * See projection.h. A port of web/js/projection.js, line for line.
 * ===================================================================== */
#include <math.h>

#include "projection.h"

float proj_scale_at(float z) {
    return FOCAL / (z + FOCAL);
}

float proj_depth_at(float s) {
    return FOCAL / s - FOCAL;
}

ProjPoint proj_point(float wx, float wy, float wz, float cam_x, float cam_y) {
    ProjPoint p;
    p.s = proj_scale_at(wz);
    p.x = CANVAS_W / 2.0f + (wx - cam_x) * p.s + cam_x * PARALLAX;
    p.y = HORIZON_Y + (wy - cam_y) * p.s + cam_y * PARALLAX;
    return p;
}

ProjPoint proj_vanishing(float cam_x, float cam_y) {
    ProjPoint p;
    p.x = CANVAS_W / 2.0f + cam_x * PARALLAX;
    p.y = HORIZON_Y + cam_y * PARALLAX;
    p.s = 0.0f;   /* a vanishing point has no size; nothing is drawn at it */
    return p;
}

int proj_in_box(float shot_x, float shot_y, float wx, float wy,
                float half_w, float half_h) {
    return fabsf(wx - shot_x) <= half_w && fabsf(wy - shot_y) <= half_h;
}
