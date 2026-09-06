// player.js — The Jovian Humanitarian Conflict | MagmaCrunch Media © 2026
// The ship: flight, banking, guns, invincibility.
//
// The ship never leaves the z = 0 plane, so its x/y are world units measured
// from the rail's centre line and need no projection — the one thing in the
// game drawn without going through Project. Everything else is drawn relative
// to it.

function Player() {
    this.reset();
}

Player.prototype.reset = function () {
    this.x = 0;
    this.y = CONFIG.SHIP_Y_START;
    this.vx = 0;
    this.vy = 0;
    this.bank = 0;              // -1..1, lags vx so the roll reads as inertia
    this.shootCooldown = 0;
    this.invincible = 0;
    this.lives = CONFIG.MAX_LIVES;
    this.thrustPhase = 0;
};

/**
 * One frame of flight.
 *
 * `wantsFire` is passed in rather than read from Input directly so the headless
 * tests can fly the ship across the whole input space without a keyboard, and
 * so this file stays free of anything the vm context does not provide.
 * Returns true on the frames a shot is actually fired.
 */
Player.prototype.update = function (axisX, axisY, wantsFire, dt) {
    // Accelerate toward the stick, then bleed off. Drag is a per-frame
    // multiplier, so it is raised to dt rather than multiplied by it — the
    // difference is invisible at 60Hz and a third of the top speed at 144Hz.
    this.vx += axisX * CONFIG.SHIP_ACCEL * dt;
    this.vy += axisY * CONFIG.SHIP_ACCEL * dt;
    const drag = Math.pow(CONFIG.SHIP_DRAG, dt);
    this.vx *= drag;
    this.vy *= drag;

    const max = CONFIG.SHIP_SPEED_MAX;
    this.vx = Math.max(-max, Math.min(max, this.vx));
    this.vy = Math.max(-max, Math.min(max, this.vy));

    this.x += this.vx * dt;
    this.y += this.vy * dt;

    // Hard walls rather than a wrap: the rail has edges, and a ship that
    // reappeared on the far side would break the parallax it is driving.
    // Zeroing the velocity on contact stops it creeping while held.
    if (this.x < -CONFIG.SHIP_X_RANGE) { this.x = -CONFIG.SHIP_X_RANGE; this.vx = 0; }
    if (this.x >  CONFIG.SHIP_X_RANGE) { this.x =  CONFIG.SHIP_X_RANGE; this.vx = 0; }
    if (this.y < CONFIG.SHIP_Y_MIN) { this.y = CONFIG.SHIP_Y_MIN; this.vy = 0; }
    if (this.y > CONFIG.SHIP_Y_MAX) { this.y = CONFIG.SHIP_Y_MAX; this.vy = 0; }

    // Bank chases the lateral speed rather than the stick, so it settles a
    // beat after you stop turning instead of snapping flat.
    const targetBank = (this.vx / max) * CONFIG.BANK_MAX;
    this.bank += (targetBank - this.bank) * Math.min(1, 0.18 * dt);

    this.thrustPhase += dt;
    if (this.invincible > 0) this.invincible -= dt;
    if (this.shootCooldown > 0) this.shootCooldown -= dt;

    if (wantsFire && this.shootCooldown <= 0) {
        this.shootCooldown = CONFIG.SHOT_COOLDOWN;
        return true;
    }
    return false;
};

/** Where a shot leaves the ship. The nose, not the centre. */
Player.prototype.muzzle = function () {
    return { x: this.x, y: this.y - 2 };
};

Player.prototype.canBeHit = function () {
    return this.invincible <= 0;
};

/** Returns true if the hit actually landed (i.e. was not an i-frame). */
Player.prototype.takeHit = function () {
    if (!this.canBeHit()) return false;
    this.lives -= 1;
    this.invincible = CONFIG.INVINCIBLE_FRAMES;
    return true;
};

// ── Drawing ───────────────────────────────────────────────────────────
// Everything below this line is the only part of this file allowed to touch a
// canvas, so the simulation above can run headless.

Player.prototype.draw = function (ctx, camX, camY) {
    // Blink through the invincibility window, but on a slow enough cycle to
    // stay readable — a fast flicker on a 26px sprite just looks like a
    // rendering fault.
    if (this.invincible > 0 && Math.floor(this.invincible / 5) % 2 === 0) return;

    const C = CONFIG.COLORS;
    const p = Project.point(this.x, this.y, 0, camX, camY);
    const w = CONFIG.SHIP_W;
    const h = CONFIG.SHIP_H;
    const b = this.bank;

    ctx.save();
    ctx.translate(Math.round(p.x), Math.round(p.y));

    // Thrust, drawn first so the hull sits over it. Two plumes, flickering out
    // of phase so the flame does not pulse as one block.
    const flick = 1 + Math.sin(this.thrustPhase * 0.8) * 0.25;
    const flick2 = 1 + Math.sin(this.thrustPhase * 0.8 + 2) * 0.25;
    ctx.fillStyle = C.thrust;
    ctx.fillRect(-6, h * 0.5, 4, 7 * flick);
    ctx.fillRect(2, h * 0.5, 4, 7 * flick2);
    ctx.fillStyle = C.thrustHot;
    ctx.fillRect(-5, h * 0.5, 2, 4 * flick);
    ctx.fillRect(3, h * 0.5, 2, 4 * flick2);

    // Wings. Banking lifts one tip and drops the other — a cheap fake roll
    // that reads correctly at this size and costs no transform.
    ctx.fillStyle = C.shipSteel;
    ctx.beginPath();
    ctx.moveTo(-w / 2, 2 + b * 5);
    ctx.lineTo(-5, -2);
    ctx.lineTo(-5, 5);
    ctx.closePath();
    ctx.fill();
    ctx.beginPath();
    ctx.moveTo(w / 2, 2 - b * 5);
    ctx.lineTo(5, -2);
    ctx.lineTo(5, 5);
    ctx.closePath();
    ctx.fill();

    // Hull.
    ctx.fillStyle = C.shipHull;
    ctx.beginPath();
    ctx.moveTo(0, -h / 2 - 3);
    ctx.lineTo(5, 4);
    ctx.lineTo(-5, 4);
    ctx.closePath();
    ctx.fill();

    ctx.fillStyle = C.shipShadow;
    ctx.fillRect(-5, 4, 10, 2);

    // Canopy.
    ctx.fillStyle = C.shipGlass;
    ctx.fillRect(-2, -4, 4, 4);

    ctx.restore();
};

/**
 * The reticle: a marker out at firing depth showing where a shot will arrive.
 *
 * Drawn through the projection so it sits where the shot actually goes, which
 * is not directly above the ship once the camera has drifted.
 */
Player.prototype.drawReticle = function (ctx, camX, camY) {
    const p = Project.point(this.x, this.y, CONFIG.Z_FIRE_MAX * 0.55, camX, camY);
    const x = Math.round(p.x);
    const y = Math.round(p.y);

    ctx.strokeStyle = CONFIG.COLORS.shot;
    ctx.globalAlpha = 0.5;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(x - 5, y); ctx.lineTo(x - 2, y);
    ctx.moveTo(x + 2, y); ctx.lineTo(x + 5, y);
    ctx.moveTo(x, y - 5); ctx.lineTo(x, y - 2);
    ctx.moveTo(x, y + 2); ctx.lineTo(x, y + 5);
    ctx.stroke();
    ctx.globalAlpha = 1;
};
