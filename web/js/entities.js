// entities.js — The Jovian Humanitarian Conflict | MagmaCrunch Media © 2026
// Contacts, shots, particles, popups — and the IFF rules.
//
// ── Why identification is built the way it is ──
//
// The premise only works if refusing to shoot is a decision rather than a
// gamble. At spawn depth a convoy hull is under 6px wide, so silhouette cannot
// carry the read, and colour alone excludes anyone who cannot separate amber
// from magenta. So a contact announces itself on four channels that come
// legible in this order:
//
//   1. Transponder blink   — every depth, including the spawn frame. Drawn at
//                            a CONSTANT screen size, so it does not shrink
//                            away. Aid convoys squawk a double-blink; hostiles
//                            are dark. This is the channel the fairness test
//                            is written against, and it is motion, not colour.
//   2. HUD contact strip   — drawn in main.js from listContacts() below, so a
//                            cluttered frame never hides the answer.
//   3. Silhouette          — from Z_SHAPE_READABLE inward: convoys are blunt
//                            and slab-sided with a bar across; hostiles are
//                            angular deltas.
//   4. Colour              — last, and never alone. Amber vs magenta, chosen
//                            to differ in luminance as well as hue.
//
// Everything here stores WORLD coordinates and converts at draw and collision
// time. Storing screen coordinates and hand-scrolling them would tie the
// entities to one particular camera drift and tear them off the rail the
// moment it changed.

function Entities() {
    this.reset();
}

Entities.prototype.reset = function () {
    this.contacts = [];
    this.shots = [];
    this.particles = [];
    this.popups = [];
    this.spawnTimer = 0;
    this.nextId = 1;
    this.wavesSpawned = 0;

    // Drained by main.js each frame. Events rather than callbacks so the
    // simulation stays free of scoring policy and stays testable.
    this.events = [];
};

// ── Spawning ──────────────────────────────────────────────────────────

/**
 * n lateral spawn positions, every pair at least SPAWN_MIN_SEPARATION apart.
 *
 * Rejection sampling was the obvious way to do this and it does not work.
 * Placing three contacts 62 units apart across a 300-unit rail fails often
 * enough that a bounded retry loop regularly gave up and stacked a convoy on
 * a hostile — precisely the frame where telling them apart has to be possible.
 *
 * This cannot fail: reserve the gaps first, draw n uniform values from the
 * slack that remains, sort them, and hand the reserved gap back as you go. The
 * result is uniform over every legal arrangement, needs no retries, and the
 * minimum separation is a property of the construction rather than a hope.
 */
Entities.prototype.spreadX = function (n, random) {
    const W = CONFIG.SPAWN_X_RANGE * 2;
    // Should a wave ever be large enough that the full gaps cannot fit, share
    // the rail out evenly instead of returning positions that break the rule.
    const gap = Math.min(CONFIG.SPAWN_MIN_SEPARATION, n > 1 ? W / (n - 1) : W);
    const slack = Math.max(0, W - (n - 1) * gap);

    const u = [];
    for (let i = 0; i < n; i++) u.push(random() * slack);
    u.sort((a, b) => a - b);

    return u.map((v, i) => -CONFIG.SPAWN_X_RANGE + v + i * gap);
};

/** Release a wave. */
Entities.prototype.spawnWave = function (t, rand) {
    const random = rand || Math.random;
    // The opening wave is scripted to show one of each, so it needs two slots
    // regardless of what the curve would have chosen.
    const n = this.wavesSpawned === 0 ? 2 : Difficulty.waveSize(t, random);
    if (n <= 0) return;

    const xs = this.spreadX(n, random);
    const aidShare = Difficulty.aidShare(t);

    const kinds = [];
    for (let i = 0; i < n; i++) kinds.push(random() < aidShare ? 'aid' : 'hostile');

    // The opening wave is one of each, always.
    //
    // Left to the dice at a 30% convoy share and two per wave, the first convoy
    // took about fifteen seconds to turn up — so the game opened by teaching
    // that everything in the sky is a target, and only then introduced the one
    // rule that contradicts it. Showing both side by side in the first wave,
    // with the separation rule guaranteeing a gap between them, is the whole
    // tutorial this game needs and it costs one branch.
    if (this.wavesSpawned === 0) {
        kinds[0] = 'hostile';
        kinds[1] = 'aid';
    } else {
        // A wave with nothing to shoot, on a screen with nothing to shoot,
        // leaves the player holding fire at empty sky — which reads as the game
        // having stalled rather than as restraint being asked of them.
        const noneHostile = !this.contacts.some(c => c.kind === 'hostile' && !c.dead);
        if (noneHostile && kinds.indexOf('hostile') === -1) kinds[n - 1] = 'hostile';
    }
    this.wavesSpawned++;

    // spreadX returns positions in ascending order, so pairing them with kinds
    // directly would put convoys on the left of every wave. Shuffle first.
    for (let i = xs.length - 1; i > 0; i--) {
        const j = Math.floor(random() * (i + 1));
        const tmp = xs[i]; xs[i] = xs[j]; xs[j] = tmp;
    }

    for (let i = 0; i < n; i++) {
        const isAid = kinds[i] === 'aid';
        this.contacts.push({
            id: this.nextId++,
            kind: isAid ? 'aid' : 'hostile',
            x: xs[i],
            y: (random() * 2 - 1) * CONFIG.CONTACT_Y_SPREAD,
            z: CONFIG.Z_FAR,
            // Drift phase is seeded per contact so a wave does not weave in
            // lockstep, which looks mechanical.
            phase: random() * Math.PI * 2,
            driftSeed: 0.6 + random() * 0.8,
            aggro: !isAid && random() < Difficulty.aggro(t),
            // Set when a hostile locks this convoy; counts down to its loss.
            doomTimer: 0,
            lockedBy: 0,
            dead: false,
            // Set once the convoy has announced itself audibly, so the ping
            // fires exactly once per contact rather than every frame it is
            // inside Z_PING.
            pinged: false,
            // Frames alive, which is what the telegraph guarantee is measured in.
            age: 0,
        });
    }
};

// ── Per-frame update ──────────────────────────────────────────────────

Entities.prototype.update = function (player, railSpeed, t, dt, rand) {
    const random = rand || Math.random;

    this.spawnTimer -= dt;
    if (this.spawnTimer <= 0) {
        this.spawnWave(t, random);
        this.spawnTimer = Difficulty.spawnInterval(t);
    }

    this.updateContacts(player, railSpeed, dt);
    this.updateShots(dt);
    this.updateParticles(dt);
    this.updatePopups(dt);
};

Entities.prototype.updateContacts = function (player, railSpeed, dt) {
    for (const c of this.contacts) {
        c.age += dt;
        c.z -= railSpeed * dt;
        c.phase += 0.04 * dt * c.driftSeed;

        const drift = c.kind === 'aid' ? CONFIG.AID_DRIFT : CONFIG.HOSTILE_DRIFT;
        c.x += Math.cos(c.phase) * drift * dt;

        // A convoy announces itself once, on the way in. main.js turns this
        // into the transponder ping; the simulation only reports that the
        // contact crossed the line, so this stays testable and audio-free.
        if (c.kind === 'aid' && !c.pinged && c.z <= CONFIG.Z_PING) {
            c.pinged = true;
            this.events.push({ type: 'aid-sighted', contact: c });
        }

        // An aggressive hostile slides toward the nearest convoy rather than
        // weaving, which is what makes escorting an active job: the convoy is
        // not merely something you refrain from shooting, it is something
        // being shot at.
        if (c.kind === 'hostile' && c.aggro) {
            const target = this.nearestAid(c);
            if (target) {
                c.x += Math.sign(target.x - c.x) * CONFIG.HOSTILE_DRIFT * 0.55 * dt;
                if (Math.abs(target.x - c.x) < 28 && target.doomTimer <= 0
                    && Math.abs(target.z - c.z) < CONFIG.LOCK_MAX_DZ) {
                    target.doomTimer = CONFIG.AID_KILL_FRAMES;
                    target.lockedBy = c.id;
                    this.events.push({ type: 'aid-locked', contact: target });
                }
            }
        }

        // A convoy under fire dies unless its attacker does first. Clearing
        // the lock when the attacker is gone is what makes the rescue land.
        if (c.kind === 'aid' && c.doomTimer > 0) {
            const attacker = this.contacts.find(h => h.id === c.lockedBy && !h.dead);
            if (!attacker) {
                c.doomTimer = 0;
                c.lockedBy = 0;
            } else {
                c.doomTimer -= dt;
                if (c.doomTimer <= 0) {
                    c.dead = true;
                    this.explode(c, CONFIG.COLORS.aid);
                    this.events.push({ type: 'aid-lost', contact: c });
                }
            }
        }

        // Collision with the player, in the z = 0 plane. Only hostiles ram;
        // flying through a convoy is not a punishable act.
        if (!c.dead && c.kind === 'hostile' && c.z < 14 && c.z > -14 && player.canBeHit()) {
            if (Math.abs(c.x - player.x) < (CONFIG.HOSTILE_W + CONFIG.SHIP_W) / 2 &&
                Math.abs(c.y - player.y) < (CONFIG.HOSTILE_H + CONFIG.SHIP_H) / 2) {
                c.dead = true;
                this.explode(c, CONFIG.COLORS.hostile);
                this.events.push({ type: 'player-hit', contact: c });
            }
        }
    }

    // Retire what has passed the camera. A convoy that makes it out the far
    // side alive is an escort earned.
    const kept = [];
    for (const c of this.contacts) {
        if (c.dead) continue;
        if (c.z <= CONFIG.Z_NEAR) {
            if (c.kind === 'aid') this.events.push({ type: 'aid-escorted', contact: c });
            continue;
        }
        kept.push(c);
    }
    this.contacts = kept;
};

Entities.prototype.nearestAid = function (from) {
    let best = null;
    let bestD = Infinity;
    for (const c of this.contacts) {
        if (c.kind !== 'aid' || c.dead) continue;
        const d = Math.abs(c.z - from.z);
        if (d < bestD) { bestD = d; best = c; }
    }
    return best;
};

// ── Shots ─────────────────────────────────────────────────────────────

Entities.prototype.fire = function (player) {
    const m = player.muzzle();
    this.shots.push({ x: m.x, y: m.y, z: 0 });
};

/**
 * Advance shots and resolve hits.
 *
 * The test is done in world space at the shot's own depth, not on screen, so
 * a target centred under the reticle is centred at every distance. Doing it on
 * screen would make near targets easier to hit purely because their sprites
 * are bigger, which would quietly punish holding fire — exactly backwards for
 * this game.
 */
Entities.prototype.updateShots = function (dt) {
    const kept = [];
    for (const s of this.shots) {
        const fromZ = s.z;
        s.z += CONFIG.SHOT_SPEED * dt;
        if (fromZ > CONFIG.Z_FIRE_MAX) continue;

        // Sweep the depth the shot covered this frame rather than testing the
        // single point it ended on: at SHOT_SPEED 26 a shot skips 26 units per
        // frame, which is wider than a hull, so a point test lets shots pass
        // through contacts at some frame rates and not others.
        const HULL_Z = 10;
        let hit = null;
        for (const c of this.contacts) {
            if (c.dead) continue;
            if (c.z < fromZ - HULL_Z || c.z > s.z + HULL_Z) continue;
            const box = this.hitBox(c);
            if (!Project.inBox(s.x, s.y, c.x, c.y, box.halfW, box.halfH)) continue;
            // Take the NEAREST candidate, not the first one in the array.
            // Array order is spawn order, so without this a convoy that
            // happened to spawn earlier would absorb a shot aimed at a hostile
            // in front of it — friendly fire decided by allocation order, which
            // is both unfair and untestable.
            if (!hit || c.z < hit.z) hit = c;
        }

        if (hit) {
            hit.dead = true;
            if (hit.kind === 'aid') {
                this.explode(hit, CONFIG.COLORS.aid);
                // Attributed to the player, not to the hostiles. The penalty
                // is severe enough that getting this wrong would be the
                // cruellest bug in the game, so it is asserted by test.
                this.events.push({ type: 'friendly-fire', contact: hit });
            } else {
                this.explode(hit, CONFIG.COLORS.hostile);
                this.events.push({ type: 'hostile-killed', contact: hit });
            }
            continue;
        }
        kept.push(s);
    }
    this.shots = kept;

    // Dead contacts are removed here as well as in updateContacts, so a kill
    // cannot be scored twice by a second shot arriving the same frame.
    this.contacts = this.contacts.filter(c => !c.dead);
};

/**
 * The box a shot must pass through to hit this contact.
 *
 * One place, so collision and anything that wants to draw or assert it cannot
 * disagree. The margins are in CONFIG and favour the player on both sides:
 * generous on hostiles, honest on convoys.
 */
Entities.prototype.hitBox = function (c) {
    const isAid = c.kind === 'aid';
    return {
        halfW: (isAid ? CONFIG.AID_W : CONFIG.HOSTILE_W) / 2
             + (isAid ? CONFIG.AID_HIT_MARGIN_W : CONFIG.HOSTILE_HIT_MARGIN_W),
        halfH: (isAid ? CONFIG.AID_H : CONFIG.HOSTILE_H) / 2
             + (isAid ? CONFIG.AID_HIT_MARGIN_H : CONFIG.HOSTILE_HIT_MARGIN_H),
    };
};

// ── Effects ───────────────────────────────────────────────────────────

Entities.prototype.explode = function (c, color) {
    for (let i = 0; i < CONFIG.PARTICLE_COUNT; i++) {
        const a = (Math.PI * 2 * i) / CONFIG.PARTICLE_COUNT;
        this.particles.push({
            x: c.x, y: c.y, z: c.z,
            vx: Math.cos(a) * CONFIG.PARTICLE_SPEED,
            vy: Math.sin(a) * CONFIG.PARTICLE_SPEED,
            life: CONFIG.PARTICLE_LIFE,
            max: CONFIG.PARTICLE_LIFE,
            color: color,
        });
    }
};

Entities.prototype.updateParticles = function (dt) {
    const kept = [];
    for (const p of this.particles) {
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.life -= dt;
        if (p.life > 0) kept.push(p);
    }
    this.particles = kept;
};

Entities.prototype.addPopup = function (x, y, z, text, color) {
    this.popups.push({ x: x, y: y, z: z, text: text, color: color, life: 46, max: 46 });
};

Entities.prototype.updatePopups = function (dt) {
    const kept = [];
    for (const p of this.popups) {
        p.y -= 0.5 * dt;
        p.life -= dt;
        if (p.life > 0) kept.push(p);
    }
    this.popups = kept;
};

// ── IFF ───────────────────────────────────────────────────────────────

/**
 * Is this contact's transponder lit on the given frame?
 *
 * A double-tap rather than a single pulse, because one blink at 2Hz is easy to
 * mistake for a rendering artefact among moving sprites, and two is not.
 * Hostiles never light — the absence is the signal, which costs no pixels and
 * cannot be confused with a dim convoy.
 */
Entities.prototype.beaconLit = function (c, frame) {
    if (c.kind !== 'aid') return false;
    const p = frame % CONFIG.BLINK_PERIOD;
    return p < CONFIG.BLINK_ON_1 || (p >= CONFIG.BLINK_GAP && p < CONFIG.BLINK_ON_2);
};

/** Contacts for the HUD strip, nearest last so near ticks draw over far ones. */
Entities.prototype.listContacts = function () {
    return this.contacts
        .filter(c => !c.dead)
        .slice()
        .sort((a, b) => b.z - a.z);
};

Entities.prototype.drainEvents = function () {
    const e = this.events;
    this.events = [];
    return e;
};

// ── Drawing ───────────────────────────────────────────────────────────

Entities.prototype.draw = function (ctx, camX, camY, frame) {
    // Far to near, so nearer contacts occlude what is behind them.
    const ordered = this.contacts.slice().sort((a, b) => b.z - a.z);
    for (const c of ordered) this.drawContact(ctx, c, camX, camY, frame);

    // Drawn over the contacts, because it is the most urgent thing on screen
    // and must not end up behind a hull that happens to be nearer.
    this.drawLocks(ctx, camX, camY, frame);

    this.drawShots(ctx, camX, camY);
    this.drawParticles(ctx, camX, camY);
    this.drawPopups(ctx, camX, camY);
};

Entities.prototype.drawContact = function (ctx, c, camX, camY, frame) {
    const C = CONFIG.COLORS;
    const p = Project.point(c.x, c.y, c.z, camX, camY);
    if (p.s <= 0) return;

    const isAid = c.kind === 'aid';
    const w = (isAid ? CONFIG.AID_W : CONFIG.HOSTILE_W) * p.s;
    const h = (isAid ? CONFIG.AID_H : CONFIG.HOSTILE_H) * p.s;
    const x = Math.round(p.x);
    const y = Math.round(p.y);

    if (isAid) {
        // Blunt slab with a bar across it — legible as "not a fighter" from
        // Z_SHAPE_READABLE inward, well before it can be shot.
        ctx.fillStyle = C.aidDark;
        ctx.fillRect(x - w / 2, y - h / 2, w, h);
        ctx.fillStyle = C.aid;
        ctx.fillRect(x - w / 2, y - h / 2, w, Math.max(1, h * 0.55));
        ctx.fillStyle = C.aidPale;
        // The cross bar. Kept at least a pixel so it survives the far distances.
        ctx.fillRect(x - w * 0.09, y - h / 2, Math.max(1, w * 0.18), h);
        ctx.fillRect(x - w / 2, y - h * 0.09, w, Math.max(1, h * 0.18));

        // A convoy under fire gets its own alarm, separate from the beacon:
        // this is the thing you are being asked to save.
        if (c.doomTimer > 0 && Math.floor(frame / 4) % 2 === 0) {
            ctx.strokeStyle = C.warn;
            ctx.lineWidth = 1;
            ctx.strokeRect(Math.round(x - w / 2) - 3, Math.round(y - h / 2) - 3,
                Math.round(w) + 6, Math.round(h) + 6);
        }
    } else {
        // Angular delta, nose toward the camera.
        ctx.fillStyle = C.hostileDark;
        ctx.beginPath();
        ctx.moveTo(x, y + h / 2);
        ctx.lineTo(x - w / 2, y - h / 2);
        ctx.lineTo(x + w / 2, y - h / 2);
        ctx.closePath();
        ctx.fill();
        ctx.fillStyle = C.hostile;
        ctx.beginPath();
        ctx.moveTo(x, y + h * 0.28);
        ctx.lineTo(x - w * 0.32, y - h / 2);
        ctx.lineTo(x + w * 0.32, y - h / 2);
        ctx.closePath();
        ctx.fill();
        if (p.s > 0.4) {
            ctx.fillStyle = C.hostileEye;
            ctx.fillRect(x - 1, y - h * 0.2, 2, 2);
        }
    }

    // ── The transponder ──
    // Drawn at a CONSTANT screen size regardless of depth. That is the whole
    // point: it is the one channel that does not shrink into illegibility, so
    // a convoy is identifiable on the frame it appears.
    if (this.beaconLit(c, frame)) {
        Renderer.glow(ctx, x, y - h / 2 - 4, 7, '255,255,255', 0.5);
        ctx.fillStyle = C.aidBeacon;
        ctx.fillRect(x - 1, Math.round(y - h / 2) - 5, 3, 3);
    }
};

/**
 * A convoy under attack, and the hostile doing it.
 *
 * Without this the escort rule was unplayable rather than merely hard. A
 * flashing box appeared round a convoy and there was nothing to say what it
 * meant, which of the several hostiles on screen had caused it, or how long
 * was left. The rule was "kill the attacker", and the game showed you neither
 * the attacker nor the clock.
 *
 * So: a line from the convoy to its attacker, a ring round the attacker, and a
 * bar that empties as the convoy runs out of time. Between them they answer
 * what, who and how long.
 */
Entities.prototype.drawLocks = function (ctx, camX, camY, frame) {
    const C = CONFIG.COLORS;

    for (const c of this.contacts) {
        if (c.kind !== 'aid' || c.doomTimer <= 0) continue;
        const attacker = this.contacts.find(h => h.id === c.lockedBy && !h.dead);
        if (!attacker) continue;

        const v = Project.point(c.x, c.y, c.z, camX, camY);
        const a = Project.point(attacker.x, attacker.y, attacker.z, camX, camY);

        // The link. Pulsed rather than solid so it reads as an alarm and does
        // not compete with the rails behind it.
        ctx.strokeStyle = C.warn;
        ctx.globalAlpha = 0.45 + 0.35 * Math.sin(frame * 0.35);
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(Math.round(v.x), Math.round(v.y));
        ctx.lineTo(Math.round(a.x), Math.round(a.y));
        ctx.stroke();

        // A ring on the attacker: this is the one to shoot.
        const ar = Math.max(5, Math.round(CONFIG.HOSTILE_W * a.s * 0.8));
        ctx.beginPath();
        ctx.arc(Math.round(a.x), Math.round(a.y), ar, 0, Math.PI * 2);
        ctx.stroke();
        ctx.globalAlpha = 1;

        // How long the convoy has. Empties left to right.
        const frac = Math.max(0, Math.min(1, c.doomTimer / CONFIG.AID_KILL_FRAMES));
        const w = Math.max(8, Math.round(CONFIG.AID_W * v.s));
        const bx = Math.round(v.x - w / 2);
        const by = Math.round(v.y + (CONFIG.AID_H * v.s) / 2) + 4;
        ctx.fillStyle = 'rgba(5,6,15,0.7)';
        ctx.fillRect(bx, by, w, 3);
        ctx.fillStyle = C.warn;
        ctx.fillRect(bx, by, Math.round(w * frac), 3);
    }
};

Entities.prototype.drawShots = function (ctx, camX, camY) {
    for (const s of this.shots) {
        const p = Project.point(s.x, s.y, s.z, camX, camY);
        const r = Math.max(1, Math.round(3 * p.s));
        ctx.fillStyle = CONFIG.COLORS.shot;
        ctx.fillRect(Math.round(p.x) - r, Math.round(p.y) - r * 2, r * 2, r * 4);
        ctx.fillStyle = CONFIG.COLORS.shotCore;
        ctx.fillRect(Math.round(p.x) - 1, Math.round(p.y) - r, 2, r * 2);
    }
};

Entities.prototype.drawParticles = function (ctx, camX, camY) {
    for (const p of this.particles) {
        const s = Project.point(p.x, p.y, p.z, camX, camY);
        ctx.globalAlpha = Math.max(0, p.life / p.max);
        ctx.fillStyle = p.color;
        const size = Math.max(1, Math.round(2 * s.s));
        ctx.fillRect(Math.round(s.x), Math.round(s.y), size, size);
    }
    ctx.globalAlpha = 1;
};

Entities.prototype.drawPopups = function (ctx, camX, camY) {
    ctx.font = '6px "Press Start 2P", monospace';
    ctx.textAlign = 'center';
    for (const p of this.popups) {
        const s = Project.point(p.x, p.y, p.z, camX, camY);
        ctx.globalAlpha = Math.min(1, p.life / (p.max * 0.5));
        ctx.fillStyle = p.color;
        ctx.fillText(p.text, Math.round(s.x), Math.round(s.y));
    }
    ctx.globalAlpha = 1;
    ctx.textAlign = 'left';
};
