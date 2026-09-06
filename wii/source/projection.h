/* =====================================================================
 * projection.h -- The Jovian Humanitarian Conflict (Wii)
 * The pseudo-3D transform. Pure maths: no GRRLIB, no libogc, no state.
 *
 * A port of web/js/projection.js, and separate from the renderer for the same
 * reason it is there: three unrelated things depend on it agreeing with
 * itself. The rail draws its cloud deck through it, contacts draw through it,
 * and collision converts the ship's box into world units at a target's depth.
 * A transform that disagreed between drawing and hitting would produce shots
 * that visibly connect and do nothing, which is the least debuggable class of
 * bug in a shooter.
 *
 * Being pure, it is also the one part that can be asserted outright rather
 * than eyeballed -- which on a console, where "eyeballed" means an emulator
 * and a screenshot, is worth more than it is in a browser.
 *
 * Everything here is in the DESIGN frame (CANVAS_W x CANVAS_H). Mapping that
 * onto whatever the TV is doing is render.c's job and happens after.
 * ===================================================================== */
#ifndef PROJECTION_H
#define PROJECTION_H

#include "config.h"

typedef struct {
    float x, y;
    /* The scale alongside the point, because every caller needs it to size
       whatever it is about to draw. Recomputing it is both wasteful and a
       chance for the two to drift apart. */
    float s;
} ProjPoint;

/* Perspective divide. Exactly 1.0 at the player's own plane and falling off
 * hyperbolically with depth. Monotonically decreasing for all z > -FOCAL,
 * which is what lets the draw order be a plain sort on z. */
float proj_scale_at(float z);

/* Inverse of proj_scale_at: the depth at which the world is drawn at `s`. */
float proj_depth_at(float s);

/* World point -> design-frame point.
 *
 * cam_x / cam_y are the camera's drift, which trails the ship. The world
 * shifts by -cam * scale (ordinary parallax) while the vanishing point itself
 * slides the OTHER way by cam * PARALLAX. That second term is what sells the
 * bank -- leaning left swings the whole horizon right, and without it the rail
 * reads as a flat scrolling backdrop no matter how correct the divide is. */
ProjPoint proj_point(float wx, float wy, float wz, float cam_x, float cam_y);

/* Where the vanishing point currently sits. proj_point(cam_x, cam_y, z) lands
 * here for every z -- the definition of a vanishing point, and asserted as
 * such in the tests. */
ProjPoint proj_vanishing(float cam_x, float cam_y);

/* Does a shot fired from (shot_x, shot_y) pass through the box a contact
 * occupies at (wx, wy)?
 *
 * Shots run parallel to the z axis from the ship's plane, so this is a 2D test
 * in the z = 0 frame and does not involve the screen at all. Doing it in world
 * space is what keeps aiming honest at every depth: a target that looks
 * centred under the reticle is centred, rather than being easier to hit up
 * close because its sprite is bigger.
 *
 * A box rather than a radius because every sprite here is wider than it is
 * tall. One circle sized to the width reaches far above and below a hull that
 * is not there; sized to the height it misses the wingtips. */
int proj_in_box(float shot_x, float shot_y, float wx, float wy,
                float half_w, float half_h);

#endif
