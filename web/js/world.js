// world.js — The Jovian Humanitarian Conflict | MagmaCrunch Media © 2026
// The rail itself: camera drift, the receding cloud deck, the gas giant, stars.
//
// The world holds no geometry that anything collides with — it is entirely
// depth cueing. That is not decoration: at 480x270 with a hyperbolic scale,
// the difference between "ships are growing" and "I am flying at them" is
// carried almost wholly by the deck bands streaming past.

function World() {
    this.reset();
}

World.prototype.reset = function () {
    this.distance = 0;
    this.camX = 0;
    this.camY = 0;

    // Deck bands recycle rather than being spawned and culled: a fixed ring of
    // depths, each wrapping back to Z_FAR as it passes the camera. Constant
    // memory, no allocation in the loop, and the spacing cannot drift.
    this.bands = [];
    const count = 30;
    for (let i = 0; i < count; i++) {
        this.bands.push(CONFIG.DECK_Z_SPAN * (i / count));
    }

    // Stars are fixed to the backdrop, not the rail — they are meant to read
    // as infinitely far away, so they only respond to camera drift.
    this.stars = [];
    for (let i = 0; i < 46; i++) {
        this.stars.push({
            x: Math.random() * CONFIG.CANVAS_W,
            y: Math.random() * CONFIG.HORIZON_Y,
            b: 0.25 + Math.random() * 0.6,
        });
    }
};

/**
 * Advance the rail one frame.
 *
 * The camera trails the ship by a fraction of the gap per frame. Because that
 * is a proportional chase rather than a fixed step, it has to be raised to dt
 * — a straight multiply overshoots badly at low frame rates and the horizon
 * visibly wobbles.
 */
World.prototype.update = function (player, railSpeed, dt) {
    this.distance += railSpeed * dt;

    const followX = player.x;
    const followY = player.y * 0.5;
    const k = 1 - Math.pow(1 - CONFIG.CAM_LAG, dt);
    this.camX += (followX - this.camX) * k;
    this.camY += (followY - this.camY) * k;

    for (let i = 0; i < this.bands.length; i++) {
        this.bands[i] -= railSpeed * dt;
        // Wrap by adding the span rather than assigning it, so the even
        // spacing survives a large dt instead of collapsing into a clump.
        while (this.bands[i] <= 0) this.bands[i] += CONFIG.DECK_Z_SPAN;
    }
};

// ── Drawing ───────────────────────────────────────────────────────────

World.prototype.draw = function (ctx) {
    const C = CONFIG.COLORS;
    const W = CONFIG.CANVAS_W;

    // Void above the horizon.
    const sky = ctx.createLinearGradient(0, 0, 0, CONFIG.HORIZON_Y);
    sky.addColorStop(0, C.void);
    sky.addColorStop(1, C.voidHaze);
    ctx.fillStyle = sky;
    ctx.fillRect(0, 0, W, CONFIG.HORIZON_Y);

    this.drawStars(ctx);
    this.drawGiant(ctx);
    this.drawDeck(ctx);
};

World.prototype.drawStars = function (ctx) {
    ctx.fillStyle = CONFIG.COLORS.star;
    for (const s of this.stars) {
        ctx.globalAlpha = s.b;
        // Stars shift a little against the camera so the void is not a decal.
        const x = s.x - this.camX * 0.06;
        ctx.fillRect(Math.round((x + CONFIG.CANVAS_W) % CONFIG.CANVAS_W), Math.round(s.y), 1, 1);
    }
    ctx.globalAlpha = 1;
};

/**
 * The gas giant, low and left behind the rail.
 *
 * Bands are horizontal slabs of a fixed palette rather than a gradient: at
 * 270px tall a smooth ramp turns to mud, and Jupiter reads as Jupiter because
 * of the banding, not the colour.
 */
World.prototype.drawGiant = function (ctx) {
    const C = CONFIG.COLORS;
    const cx = 96 - this.camX * 0.12;
    const cy = CONFIG.HORIZON_Y - 6;
    const r = 78;

    ctx.save();
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.clip();

    const bands = [
        [-1.00, -0.72, C.bandShadow],
        [-0.72, -0.50, C.bandTan],
        [-0.50, -0.30, C.bandCream],
        [-0.30, -0.10, C.bandUmber],
        [-0.10, 0.14, C.bandTan],
        [0.14, 0.36, C.bandCream],
        [0.36, 0.58, C.bandUmber],
        [0.58, 1.00, C.bandDeep],
    ];
    for (const [a, b, col] of bands) {
        ctx.fillStyle = col;
        ctx.fillRect(cx - r, cy + a * r, r * 2, (b - a) * r + 1);
    }

    // The Spot, sitting in the umber band below the equator.
    ctx.fillStyle = C.spotRed;
    ctx.beginPath();
    ctx.ellipse(cx - 22, cy + 26, 20, 9, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = C.spotCore;
    ctx.beginPath();
    ctx.ellipse(cx - 22, cy + 26, 11, 4.5, 0, 0, Math.PI * 2);
    ctx.fill();

    // Terminator: the limb away from the rail falls into shadow.
    const shade = ctx.createLinearGradient(cx - r, 0, cx + r, 0);
    shade.addColorStop(0, 'rgba(5,6,15,0.62)');
    shade.addColorStop(0.45, 'rgba(5,6,15,0)');
    shade.addColorStop(1, 'rgba(5,6,15,0.30)');
    ctx.fillStyle = shade;
    ctx.fillRect(cx - r, cy - r, r * 2, r * 2);

    ctx.restore();
};

/**
 * The cloud deck: the rail's floor, drawn as bands streaming toward the camera.
 *
 * Each band is one horizontal strip whose screen y and width come straight
 * from the projection, so a band's motion up the screen is the same hyperbolic
 * curve the ships follow. Drawn far to near so nearer bands overlap the haze
 * behind them.
 */
World.prototype.drawDeck = function (ctx) {
    const C = CONFIG.COLORS;
    const W = CONFIG.CANVAS_W;
    const H = CONFIG.CANVAS_H;

    // Deck ground, horizon to the bottom of the frame.
    const deck = ctx.createLinearGradient(0, CONFIG.HORIZON_Y, 0, H);
    deck.addColorStop(0, C.deckFar);
    deck.addColorStop(1, C.deckNear);
    ctx.fillStyle = deck;
    ctx.fillRect(0, CONFIG.HORIZON_Y, W, H - CONFIG.HORIZON_Y);

    // Sort a copy far-to-near. 16 items, once a frame — the clarity is worth
    // more than the allocation.
    const ordered = this.bands.slice().sort((a, b) => b - a);

    const DECK_Y = 150;      // world y of the deck surface, below the ship
    const HALF = 900;        // world half-width of a band

    for (const z of ordered) {
        const l = Project.point(-HALF, DECK_Y, z, this.camX, this.camY);
        const r = Project.point(HALF, DECK_Y, z, this.camX, this.camY);
        if (l.y < CONFIG.HORIZON_Y || l.y > H) continue;

        // Near bands are brighter and thicker; far ones fade into the haze.
        const t = 1 - z / CONFIG.DECK_Z_SPAN;
        ctx.globalAlpha = 0.10 + t * 0.42;
        ctx.fillStyle = C.deckLine;
        ctx.fillRect(Math.round(l.x), Math.round(l.y), Math.round(r.x - l.x), Math.max(1, Math.round(1 + t * 2)));
    }
    ctx.globalAlpha = 1;

    // Two rails converging on the vanishing point, which is what makes the
    // camera's lateral drift legible — without them, sliding left and sliding
    // the whole world right look identical.
    const v = Project.vanishing(this.camX, this.camY);
    ctx.strokeStyle = C.deckLine;
    ctx.globalAlpha = 0.30;
    ctx.lineWidth = 1;
    for (const side of [-1, 1]) {
        const near = Project.point(side * 210, DECK_Y, 20, this.camX, this.camY);
        ctx.beginPath();
        ctx.moveTo(v.x, v.y);
        ctx.lineTo(near.x, near.y);
        ctx.stroke();
    }
    ctx.globalAlpha = 1;
};

/** The title screen's backdrop: the same world, drifting gently on its own. */
World.prototype.drawTitle = function (ctx, frame) {
    this.camX = Math.sin(frame * 0.006) * 40;
    this.camY = Math.sin(frame * 0.004) * 10;
    for (let i = 0; i < this.bands.length; i++) {
        this.bands[i] -= CONFIG.RAIL_SPEED_MIN * 0.5;
        while (this.bands[i] <= 0) this.bands[i] += CONFIG.DECK_Z_SPAN;
    }
    this.draw(ctx);
};
