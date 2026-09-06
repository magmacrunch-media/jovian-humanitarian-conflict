// projection.js — The Jovian Humanitarian Conflict | MagmaCrunch Media © 2026
// The pseudo-3D transform. Pure maths: no canvas, no DOM, no state.
//
// This lives in its own file rather than inside the renderer because three
// unrelated things depend on it agreeing with itself — the world draws its
// cloud deck through it, entities draw contacts through it, and collision
// converts the ship's screen box back into world units at a target's depth. A
// transform that disagreed between drawing and hitting would produce shots
// that visibly connect and do nothing, which is the least debuggable class of
// bug in a shooter. Being pure, it is also the one part that can be asserted
// outright rather than eyeballed.

const Project = {
    /**
     * Perspective divide.
     *
     * Exactly 1.0 at the player's own plane and falling off hyperbolically
     * with depth, so distant things bunch toward the horizon the way a real
     * lens makes them. Monotonically decreasing for all z > -FOCAL, which is
     * what lets the draw order be a plain sort on z.
     */
    scaleAt(z) {
        return CONFIG.FOCAL / (z + CONFIG.FOCAL);
    },

    /**
     * Inverse of scaleAt: the depth at which the world is drawn at `s`.
     * Used to size the cloud deck's bands from the screen rows they should
     * land on, rather than hand-tuning depths until the spacing looks right.
     */
    depthAt(s) {
        return CONFIG.FOCAL / s - CONFIG.FOCAL;
    },

    /**
     * World point -> screen point.
     *
     * camX / camY are the camera's drift, which trails the ship. The world
     * shifts by -cam * scale (ordinary parallax: near things slide further
     * than far ones) while the vanishing point itself slides the *other* way
     * by cam * PARALLAX. That second term is what sells the bank — leaning
     * left swings the whole horizon right, and without it the rail reads as a
     * flat scrolling backdrop no matter how correct the divide is.
     *
     * Returns the scale alongside the point because every caller needs it to
     * size whatever it is about to draw, and recomputing it is both wasteful
     * and a chance for the two to drift apart.
     */
    point(wx, wy, wz, camX, camY) {
        const s = this.scaleAt(wz);
        return {
            x: CONFIG.CANVAS_W / 2 + (wx - camX) * s + camX * CONFIG.PARALLAX,
            y: CONFIG.HORIZON_Y + (wy - camY) * s + camY * CONFIG.PARALLAX,
            s: s,
        };
    },

    /**
     * Where the vanishing point currently sits on screen.
     *
     * point(camX, camY, z) lands here for every z — the definition of a
     * vanishing point, and asserted as such in the tests.
     */
    vanishing(camX, camY) {
        return {
            x: CONFIG.CANVAS_W / 2 + camX * CONFIG.PARALLAX,
            y: CONFIG.HORIZON_Y + camY * CONFIG.PARALLAX,
        };
    },

    /**
     * Does a shot fired from (shotX, shotY) pass through the box a contact
     * occupies at (wx, wy)?
     *
     * Shots run parallel to the z axis from the ship's plane, so this is a 2D
     * test in the z = 0 frame and does not involve the screen at all. Doing it
     * in world space is what keeps aiming honest at every depth: a target that
     * looks centred under the reticle is centred, rather than being easier to
     * hit up close because its sprite is bigger.
     *
     * A box rather than a radius because every sprite here is wider than it is
     * tall. One circle sized to the width reaches far above and below a hull
     * that is not there; sized to the height it misses the wingtips. Both were
     * wrong in the same frame.
     */
    inBox(shotX, shotY, wx, wy, halfW, halfH) {
        return Math.abs(wx - shotX) <= halfW && Math.abs(wy - shotY) <= halfH;
    },
};
