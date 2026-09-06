/**
 * test-simulation.js — The Jovian Humanitarian Conflict headless tests.
 *
 * The game's modules are plain scripts that assign globals and touch no DOM
 * outside their draw() methods, so the real shipped files are loaded into a vm
 * context and run. Nothing here is a reimplementation that could drift from
 * what ships.
 *
 * What this guards, in order of how much it would hurt:
 *
 *  1. The fairness invariant. The whole premise is that refusing to shoot is a
 *     decision rather than a gamble, which is only true if a convoy squawks its
 *     transponder for long enough to be identified BEFORE it can be shot. That
 *     is a relationship between four constants that a plausible-looking tuning
 *     pass can silently break — raise RAIL_SPEED_MAX and the game is still fun
 *     and now unfair. Asserted on the constants and again by simulation.
 *
 *  2. Friendly-fire attribution. A convoy killed by a hostile costs a combo; one
 *     killed by the player costs 1,000 points and a third of the run. Swapping
 *     those is the cruellest bug this game could have, because it punishes the
 *     player for the thing they did right.
 *
 *  3. Frame-rate independence. Roderick Tron shipped with dt applied to gravity
 *     but not to the position step, so a 120Hz display played a different game.
 *     Every quantity here is scaled by dt, and `dt invariance` asserts three
 *     step sizes covering the same wall-clock time agree.
 *
 *  4. Projection sanity. Draw and collision both go through Project, so a
 *     transform that is not monotonic in z would produce shots that visibly
 *     connect and do nothing.
 *
 * Run: node test-simulation.js
 */

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const JS_DIR = path.join(__dirname, '..', 'js');

// ── Harness ───────────────────────────────────────────────────────────────────

let passed = 0, failed = 0;

function ok(cond, name, detail) {
    if (cond) { passed++; console.log('  PASS  ' + name); }
    else { failed++; console.log('  FAIL  ' + name + (detail ? '\n          ' + detail : '')); }
}

function near(a, b, tol, name) {
    ok(Math.abs(a - b) <= tol, name, `got ${a}, expected ${b} +/- ${tol}`);
}

/** Deterministic PRNG so a failure is reproducible and a pass is not luck. */
function mulberry32(seed) {
    let a = seed >>> 0;
    return function () {
        a |= 0; a = (a + 0x6D2B79F5) | 0;
        let t = Math.imul(a ^ (a >>> 15), 1 | a);
        t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

/** A recording no-op 2D context — enough for the draw() methods to run. */
function stubCtx() {
    const calls = [];
    const noop = (name) => (...args) => { calls.push([name, args]); };
    return {
        calls,
        fillStyle: '', strokeStyle: '', globalAlpha: 1, lineWidth: 1,
        font: '', textAlign: 'left',
        fillRect: noop('fillRect'),
        strokeRect: noop('strokeRect'),
        clearRect: noop('clearRect'),
        fillText: noop('fillText'),
        beginPath: noop('beginPath'),
        closePath: noop('closePath'),
        moveTo: noop('moveTo'),
        lineTo: noop('lineTo'),
        arc: noop('arc'),
        ellipse: noop('ellipse'),
        fill: noop('fill'),
        stroke: noop('stroke'),
        clip: noop('clip'),
        save: noop('save'),
        restore: noop('restore'),
        translate: noop('translate'),
        createLinearGradient: () => ({ addColorStop() {} }),
        createRadialGradient: () => ({ addColorStop() {} }),
    };
}

/**
 * A fresh game. Each call gets its own vm context, so state cannot leak between
 * tests and Math.random can be reseeded per scenario.
 */
function makeGame(seed) {
    const sandbox = { console };
    sandbox.Math = Object.create(Math);
    sandbox.Math.random = mulberry32(seed === undefined ? 1 : seed);
    // localStorage is never reached from the simulation modules, but a typo
    // that reached for it should fail loudly here rather than in a browser.
    const ctx = vm.createContext(sandbox);

    for (const file of ['config.js', 'projection.js', 'renderer.js', 'player.js', 'world.js', 'entities.js']) {
        vm.runInContext(fs.readFileSync(path.join(JS_DIR, file), 'utf8'), ctx, { filename: file });
    }

    const g = {
        ctx,
        CONFIG: vm.runInContext('CONFIG', ctx),
        Difficulty: vm.runInContext('Difficulty', ctx),
        Project: vm.runInContext('Project', ctx),
        player: vm.runInContext('new Player()', ctx),
        world: vm.runInContext('new World()', ctx),
        entities: vm.runInContext('new Entities()', ctx),
        rand: mulberry32((seed === undefined ? 1 : seed) + 977),
    };

    /**
     * One frame, in main.js's order. Kept in step with main.js by hand — if
     * that order changes, this must too, which is the price of testing the
     * real modules rather than a copy of them.
     */
    g.step = function (dt, axisX, axisY, firing) {
        const t = g.Difficulty.at(g.world.distance);
        const railSpeed = g.Difficulty.railSpeed(t);
        const fired = g.player.update(axisX || 0, axisY || 0, !!firing, dt);
        if (fired) g.entities.fire(g.player);
        g.world.update(g.player, railSpeed, dt);
        g.entities.update(g.player, railSpeed, t, dt, g.rand);
        return g.entities.drainEvents();
    };

    return g;
}

// ── 1. The fairness invariant ─────────────────────────────────────────────────

console.log('\nfairness — a convoy is identifiable before it can be shot');
{
    const g = makeGame(1);
    const C = g.CONFIG;

    // Two full squawk cycles is what "identifiable" means here, and the double
    // blink has to fit inside one period for that to be true.
    ok(C.BLINK_ON_2 < C.BLINK_PERIOD && C.BLINK_GAP < C.BLINK_ON_2,
        'the double blink fits inside one squawk period',
        `on1=${C.BLINK_ON_1} gap=${C.BLINK_GAP} on2=${C.BLINK_ON_2} period=${C.BLINK_PERIOD}`);

    ok(C.TELEGRAPH_MIN_FRAMES >= C.BLINK_PERIOD * 2,
        'the telegraph budget covers at least two squawk cycles',
        `budget=${C.TELEGRAPH_MIN_FRAMES} needs>=${C.BLINK_PERIOD * 2}`);

    // The invariant proper, at the WORST case: full difficulty, top rail speed.
    const budget = C.TELEGRAPH_MIN_FRAMES + C.REACTION_FRAMES;
    const available = g.Difficulty.identifyFrames(1);
    ok(available >= budget,
        'identification window survives the fastest rail',
        `have ${available.toFixed(1)} frames, need ${budget}`);

    // And at the easiest, which must not somehow be worse.
    ok(g.Difficulty.identifyFrames(0) >= g.Difficulty.identifyFrames(1),
        'the window only ever narrows with difficulty');

    // Simulated, not just arithmetic: fly a convoy from spawn and count the
    // frames it squawks before it enters firing range.
    const sim = makeGame(4);
    sim.entities.contacts.push({
        id: 1, kind: 'aid', x: 0, y: 0, z: sim.CONFIG.Z_FAR,
        phase: 0, driftSeed: 1, aggro: false, doomTimer: 0, lockedBy: 0,
        dead: false, age: 0,
    });
    const c = sim.entities.contacts[0];
    let framesBeforeFirable = 0;
    let litFrames = 0;
    let f = 0;
    const railMax = sim.Difficulty.railSpeed(1);
    while (c.z > sim.CONFIG.Z_FIRE_MAX) {
        if (sim.entities.beaconLit(c, f)) litFrames++;
        c.z -= railMax;
        framesBeforeFirable++;
        f++;
    }
    ok(framesBeforeFirable >= budget,
        'a spawned convoy is out of range for the whole telegraph budget',
        `${framesBeforeFirable} frames vs ${budget}`);
    ok(litFrames >= 10,
        'and its transponder is actually lit during that window',
        `lit on ${litFrames} of ${framesBeforeFirable} frames`);

    // Shape becomes a usable second channel while still out of range.
    ok(C.Z_SHAPE_READABLE > C.Z_FIRE_MAX - 1 || C.Z_SHAPE_READABLE < C.Z_FIRE_MAX,
        'silhouette threshold is inside the rail', `${C.Z_SHAPE_READABLE}`);
    ok(g.Project.scaleAt(C.Z_SHAPE_READABLE) >= 0.3,
        'the silhouette threshold is a size a shape can actually be read at',
        `scale ${g.Project.scaleAt(C.Z_SHAPE_READABLE).toFixed(3)}`);
}

// ── 1b. The audible transponder ────────────────────────────────────────────────

console.log('');
console.log('the ping — the transponder heard, not seen');
{
    const g = makeGame(7);
    const C = g.CONFIG;

    // It has to sound while the player still has every option, which means
    // outside firing range with the whole reaction budget left.
    ok(C.Z_PING > C.Z_FIRE_MAX,
        'the ping sounds before a convoy can be shot',
        `Z_PING=${C.Z_PING} Z_FIRE_MAX=${C.Z_FIRE_MAX}`);
    const framesAfterPing = (C.Z_PING - C.Z_FIRE_MAX) / g.Difficulty.railSpeed(1);
    ok(framesAfterPing >= C.REACTION_FRAMES,
        'and leaves at least a reaction to act on it',
        `${framesAfterPing.toFixed(1)} frames, need ${C.REACTION_FRAMES}`);

    // Exactly once per convoy. Firing every frame inside Z_PING would turn an
    // identification cue into a drone nobody hears.
    const sim = makeGame(8);
    sim.entities.contacts.push({
        id: 1, kind: 'aid', x: 0, y: 0, z: sim.CONFIG.Z_FAR,
        phase: 0, driftSeed: 1, aggro: false, doomTimer: 0, lockedBy: 0,
        dead: false, pinged: false, age: 0,
    });
    let pings = 0, firstPingZ = null;
    for (let i = 0; i < 400; i++) {
        sim.entities.updateContacts(sim.player, 4, 1);
        for (const ev of sim.entities.drainEvents()) {
            if (ev.type === 'aid-sighted') { pings++; if (firstPingZ === null) firstPingZ = ev.contact.z; }
        }
    }
    ok(pings === 1, 'a convoy pings exactly once', `pinged ${pings} times`);
    ok(firstPingZ !== null && firstPingZ > sim.CONFIG.Z_FIRE_MAX,
        'and it pings while still out of range',
        `first ping at z=${firstPingZ === null ? 'never' : Math.round(firstPingZ)}`);

    // Hostiles are silent, the same way they are dark.
    const h = makeGame(9);
    h.entities.contacts.push({
        id: 1, kind: 'hostile', x: 0, y: 0, z: h.CONFIG.Z_FAR,
        phase: 0, driftSeed: 1, aggro: false, doomTimer: 0, lockedBy: 0,
        dead: false, pinged: false, age: 0,
    });
    let hostilePings = 0;
    for (let i = 0; i < 400; i++) {
        h.entities.updateContacts(h.player, 4, 1);
        for (const ev of h.entities.drainEvents()) if (ev.type === 'aid-sighted') hostilePings++;
    }
    ok(hostilePings === 0, 'a hostile never pings', `pinged ${hostilePings} times`);

    // Every convoy in a real run gets announced, none silently slips through.
    const r = makeGame(10);
    let spawnedAid = 0, sighted = 0;
    const seen = new Set();
    for (let f = 0; f < 3000; f++) {
        const before = new Set(r.entities.contacts.map(c => c.id));
        for (const ev of r.step(1, 0, 0, false)) if (ev.type === 'aid-sighted') sighted++;
        for (const c of r.entities.contacts) {
            if (c.kind === 'aid' && !before.has(c.id) && !seen.has(c.id)) { seen.add(c.id); spawnedAid++; }
        }
    }
    ok(spawnedAid > 10, 'the run actually produced convoys to check', `${spawnedAid}`);
    ok(sighted >= spawnedAid - 2,
        'essentially every convoy announced itself',
        `${sighted} pings for ${spawnedAid} convoys`);
}

// ── 2. Friendly-fire attribution ──────────────────────────────────────────────

console.log('\nattribution — who killed the convoy');
{
    // Shot by the player.
    const g = makeGame(2);
    g.entities.contacts.push({
        id: 1, kind: 'aid', x: 0, y: 0, z: 200,
        phase: 0, driftSeed: 1, aggro: false, doomTimer: 0, lockedBy: 0,
        dead: false, age: 0,
    });
    g.entities.shots.push({ x: 0, y: 0, z: 190 });
    g.entities.updateShots(1);
    const evs = g.entities.drainEvents();
    ok(evs.length === 1 && evs[0].type === 'friendly-fire',
        'a convoy hit by the player raises friendly-fire',
        JSON.stringify(evs.map(e => e.type)));

    // Killed by a hostile lock instead.
    const h = makeGame(3);
    h.entities.contacts.push({
        id: 1, kind: 'aid', x: 0, y: 0, z: 300,
        phase: 0, driftSeed: 1, aggro: false,
        doomTimer: 1, lockedBy: 2, dead: false, age: 0,
    });
    h.entities.contacts.push({
        id: 2, kind: 'hostile', x: 10, y: 0, z: 300,
        phase: 0, driftSeed: 1, aggro: true, doomTimer: 0, lockedBy: 0,
        dead: false, age: 0,
    });
    h.entities.updateContacts(h.player, 0, 1);
    const hev = h.entities.drainEvents().map(e => e.type);
    ok(hev.includes('aid-lost') && !hev.includes('friendly-fire'),
        'a convoy killed by a hostile raises aid-lost, never friendly-fire',
        JSON.stringify(hev));

    // Killing the attacker calls the strike off — the rescue has to actually work.
    const r = makeGame(5);
    r.entities.contacts.push({
        id: 1, kind: 'aid', x: 0, y: 0, z: 300,
        phase: 0, driftSeed: 1, aggro: false,
        doomTimer: 40, lockedBy: 2, dead: false, age: 0,
    });
    r.entities.contacts.push({
        id: 2, kind: 'hostile', x: 0, y: 0, z: 260,
        phase: 0, driftSeed: 1, aggro: false, doomTimer: 0, lockedBy: 0,
        dead: false, age: 0,
    });
    r.entities.shots.push({ x: 0, y: 0, z: 252 });
    r.entities.updateShots(1);
    r.entities.drainEvents();
    r.entities.updateContacts(r.player, 0, 1);
    const survivor = r.entities.contacts.find(c => c.kind === 'aid');
    ok(survivor && survivor.doomTimer === 0,
        'killing the attacker releases the convoy it had locked',
        survivor ? `doomTimer=${survivor.doomTimer}` : 'convoy missing');

    // A convoy that reaches the camera alive is an escort, not a loss.
    const e = makeGame(6);
    e.entities.contacts.push({
        id: 1, kind: 'aid', x: 0, y: 0, z: e.CONFIG.Z_NEAR + 1,
        phase: 0, driftSeed: 1, aggro: false, doomTimer: 0, lockedBy: 0,
        dead: false, age: 0,
    });
    e.entities.updateContacts(e.player, 10, 1);
    const eev = e.entities.drainEvents().map(x => x.type);
    ok(eev.includes('aid-escorted'), 'a convoy that gets past the camera is escorted',
        JSON.stringify(eev));
}

// ── 3. dt invariance ──────────────────────────────────────────────────────────

console.log('\ndt invariance — the same wall-clock time, three step sizes');
{
    // Fly the same input for 120 frames' worth of time at 1.0, 0.5 and 0.25.
    function fly(dt, steps) {
        const g = makeGame(11);
        for (let i = 0; i < steps; i++) g.step(dt, 1, -1, false);
        return { x: g.player.x, y: g.player.y, d: g.world.distance };
    }

    const a = fly(1.0, 120);
    const b = fly(0.5, 240);
    const c = fly(0.25, 480);

    near(b.x, a.x, 1.2, 'ship x agrees between 60Hz and 120Hz');
    near(c.x, a.x, 1.8, 'ship x agrees between 60Hz and 240Hz');
    near(b.y, a.y, 1.2, 'ship y agrees between 60Hz and 120Hz');
    near(c.y, a.y, 1.8, 'ship y agrees between 60Hz and 240Hz');
    near(b.d, a.d, 0.5, 'distance travelled agrees between 60Hz and 120Hz');
    near(c.d, a.d, 0.5, 'distance travelled agrees between 60Hz and 240Hz');

    // Contact depth is what everything else is measured against, so it gets its
    // own check with no player input in play at all.
    function drift(dt, steps) {
        const g = makeGame(12);
        g.entities.contacts.push({
            id: 1, kind: 'hostile', x: 0, y: 0, z: 900,
            phase: 0, driftSeed: 1, aggro: false, doomTimer: 0, lockedBy: 0,
            dead: false, age: 0,
        });
        for (let i = 0; i < steps; i++) g.entities.updateContacts(g.player, 5, dt);
        const c0 = g.entities.contacts[0];
        return c0 ? c0.z : null;
    }
    near(drift(0.5, 200), drift(1.0, 100), 0.01, 'contact depth is dt-invariant');

    // The camera chase is proportional, so it is the one most likely to be got
    // wrong with a plain multiply.
    function cam(dt, steps) {
        const g = makeGame(13);
        g.player.x = 100;
        for (let i = 0; i < steps; i++) g.world.update(g.player, 5, dt);
        return g.world.camX;
    }
    near(cam(0.5, 60), cam(1.0, 30), 0.6, 'camera drift is dt-invariant');

    // Drag is a per-frame multiplier and must be raised to dt, not scaled by it.
    function coast(dt, steps) {
        const g = makeGame(14);
        for (let i = 0; i < 20; i++) g.step(1.0, 1, 0, false);
        const before = g.player.vx;
        for (let i = 0; i < steps; i++) g.player.update(0, 0, false, dt);
        return { before, after: g.player.vx };
    }
    const c1 = coast(1.0, 20);
    const c2 = coast(0.5, 40);
    near(c2.after, c1.after, 0.05, 'velocity decay is dt-invariant');
}

// ── 4. Projection ─────────────────────────────────────────────────────────────

console.log('\nprojection');
{
    const g = makeGame(21);
    const P = g.Project;
    const C = g.CONFIG;

    near(P.scaleAt(0), 1, 1e-9, 'scale is exactly 1 at the ship plane');

    let monotonic = true;
    let prev = Infinity;
    for (let z = 0; z <= C.Z_FAR; z += 10) {
        const s = P.scaleAt(z);
        if (s >= prev) { monotonic = false; break; }
        prev = s;
    }
    ok(monotonic, 'scale decreases monotonically with depth');

    ok(P.scaleAt(C.Z_FAR) > 0, 'nothing on the rail projects to zero or behind');

    // scaleAt and depthAt must be true inverses, or the deck bands land at
    // depths that do not match where they are drawn.
    for (const z of [0, 120, 410, 600, 1100]) {
        near(P.depthAt(P.scaleAt(z)), z, 1e-6, `depthAt inverts scaleAt at z=${z}`);
    }

    // The defining property of a vanishing point: the camera's own axis lands
    // on the same screen pixel at every depth.
    const v = P.vanishing(40, 12);
    let allAgree = true;
    for (const z of [0, 200, 600, 1100]) {
        const p = P.point(40, 12, z, 40, 12);
        if (Math.abs(p.x - v.x) > 1e-9 || Math.abs(p.y - v.y) > 1e-9) { allAgree = false; break; }
    }
    ok(allAgree, 'the camera axis lands on the vanishing point at every depth');

    // Aiming is done in world space so it cannot get easier up close.
    ok(P.inBox(0, 0, 6, 0, 10, 6) && !P.inBox(0, 0, 14, 0, 10, 6),
        'inBox is a world-space box test, horizontally');
    ok(P.inBox(0, 0, 0, 5, 10, 6) && !P.inBox(0, 0, 0, 9, 10, 6),
        'and vertically');
}

// ── 4b. Hit boxes ─────────────────────────────────────────────────────────────

console.log('');
console.log('hit boxes — generous on hostiles, honest on convoys');
{
    const g = makeGame(61);
    const C = g.CONFIG;
    const hostile = { kind: 'hostile' };
    const aid = { kind: 'aid' };
    const hb = g.entities.hitBox(hostile);
    const ab = g.entities.hitBox(aid);

    // The point of the change. A hostile must be at least as easy to hit as a
    // convoy, or every near miss punishes the player twice: the target lives
    // and the thing that ends the run takes the shot instead.
    ok(hb.halfW >= ab.halfW - 1e-9,
        'a hostile is at least as wide a target as a convoy',
        `hostile ${hb.halfW} vs convoy ${ab.halfW}`);
    ok(hb.halfH >= ab.halfH - 1e-9,
        'and at least as tall',
        `hostile ${hb.halfH} vs convoy ${ab.halfH}`);

    // A hostile's box is bigger than its sprite; a convoy's is not bigger than
    // its sprite. Neither may be so large it swallows a deliberate miss.
    ok(hb.halfW > C.HOSTILE_W / 2, 'the hostile box is more generous than its hull');
    ok(ab.halfW <= C.AID_W / 2 + 1e-9,
        'the convoy box never exceeds its hull',
        `box ${ab.halfW} vs hull ${C.AID_W / 2}`);
    ok(ab.halfH <= C.AID_H / 2 + 1e-9, 'in both directions');

    // Collision actually uses it: a shot just outside a convoy's hull misses.
    const t = makeGame(62);
    t.entities.contacts.push({
        id: 1, kind: 'aid', x: 0, y: 0, z: 200,
        phase: 0, driftSeed: 1, aggro: false, doomTimer: 0, lockedBy: 0,
        dead: false, pinged: true, age: 0,
    });
    t.entities.shots.push({ x: t.CONFIG.AID_W / 2 + 3, y: 0, z: 190 });
    t.entities.updateShots(1);
    const evs = t.entities.drainEvents().map(e => e.type);
    ok(!evs.includes('friendly-fire'),
        'a shot past the edge of a convoy does not clip it',
        JSON.stringify(evs));
}

// ── 4c. The rescue window ─────────────────────────────────────────────────────

console.log('');
console.log('rescue window — long enough to win, short enough to matter');
{
    const g = makeGame(71);
    const C = g.CONFIG;

    // FLOOR, flown rather than assumed: put the ship in one corner of the rail
    // and drive it to the opposite one with the real Player.update, then add
    // the shot's flight, a reaction and a full gun cooldown. That is the
    // longest a perfect player can take to answer a lock.
    const p = g.player;
    p.x = -C.SPAWN_X_RANGE;
    p.y = C.SHIP_Y_MAX;
    let cross = 0;
    while ((p.x < C.SPAWN_X_RANGE - 2 || p.y > -C.CONTACT_Y_SPREAD + 2) && cross < 2000) {
        p.update(1, -1, false, 1);
        cross++;
    }
    const intercept = cross + C.Z_FIRE_MAX / C.SHOT_SPEED + C.REACTION_FRAMES + C.SHOT_COOLDOWN;

    ok(C.AID_KILL_FRAMES > intercept,
        'a convoy outlives the worst-case intercept, so the rescue is possible',
        `timer ${C.AID_KILL_FRAMES} vs intercept ${intercept.toFixed(0)}`);

    // CEILING: a timer longer than the rail is no timer at all, because the
    // convoy reaches the camera and escapes before it can expire.
    const railLife = (C.Z_FAR - C.Z_NEAR) / C.RAIL_SPEED_MAX;
    ok(C.AID_KILL_FRAMES < railLife,
        'and dies before it could simply fly off the end, so the threat is real',
        `timer ${C.AID_KILL_FRAMES} vs rail life ${railLife.toFixed(0)}`);

    // The two bounds have to leave room for each other. If a retune ever closes
    // this gap the mechanic cannot be made fair and meaningful at once, and the
    // fix is a faster ship or a shorter rail, not a bigger number here.
    ok(railLife > intercept,
        'the rail is long enough for an intercept to fit inside it at all',
        `rail ${railLife.toFixed(0)} vs intercept ${intercept.toFixed(0)}`);
}

// ── 5. Spawning ───────────────────────────────────────────────────────────────

console.log('\nspawning');
{
    // Separation is what stops a convoy being hidden behind a hostile at the
    // exact moment identification matters.
    let worst = Infinity;
    for (let seed = 0; seed < 40; seed++) {
        const g = makeGame(100 + seed);
        const rand = mulberry32(500 + seed);
        g.entities.spawnWave(1, rand);
        const xs = g.entities.contacts.map(c => c.x);
        for (let i = 0; i < xs.length; i++) {
            for (let j = i + 1; j < xs.length; j++) {
                worst = Math.min(worst, Math.abs(xs[i] - xs[j]));
            }
        }
    }
    const g0 = makeGame(1);
    // The full separation, not a fraction of it: spreadX guarantees this by
    // construction, so anything less means the construction was replaced by
    // something that can fail.
    ok(worst === Infinity || worst >= g0.CONFIG.SPAWN_MIN_SEPARATION - 1e-9,
        'waves keep their contacts laterally apart',
        `closest pair over 40 waves: ${worst === Infinity ? 'n/a' : worst.toFixed(1)}`);

    // Every contact spawns inside the box the ship can actually reach across,
    // or some of them can never be engaged at all.
    let allInRange = true;
    for (let seed = 0; seed < 30; seed++) {
        const g = makeGame(200 + seed);
        g.entities.spawnWave(Math.random(), mulberry32(700 + seed));
        for (const c of g.entities.contacts) {
            if (Math.abs(c.x) > g.CONFIG.SPAWN_X_RANGE + 1) { allInRange = false; break; }
        }
    }
    ok(allInRange, 'contacts spawn within the rail the ship can cover');

    // A screen with nothing hostile on it is a stalled game.
    const s = makeGame(301);
    s.entities.spawnWave(0, mulberry32(9));
    ok(s.entities.contacts.some(c => c.kind === 'hostile'),
        'the first wave always contains something to shoot');

    // The opening wave is the entire tutorial: it must show one of each, at
    // every seed, or some runs open by teaching that everything is a target.
    let alwaysBoth = true;
    let details = '';
    for (let seed = 0; seed < 50; seed++) {
        const g = makeGame(400 + seed);
        g.entities.spawnWave(0, mulberry32(1000 + seed));
        const kinds = g.entities.contacts.map(c => c.kind);
        if (!kinds.includes('aid') || !kinds.includes('hostile')) {
            alwaysBoth = false;
            details = `seed ${seed}: ${JSON.stringify(kinds)}`;
            break;
        }
    }
    ok(alwaysBoth, 'the opening wave always shows one convoy and one hostile', details);

    // And they must be far enough apart to be told apart.
    let openingGap = Infinity;
    for (let seed = 0; seed < 50; seed++) {
        const g = makeGame(500 + seed);
        g.entities.spawnWave(0, mulberry32(2000 + seed));
        const xs = g.entities.contacts.map(c => c.x);
        openingGap = Math.min(openingGap, Math.abs(xs[0] - xs[1]));
    }
    const gc = makeGame(1);
    ok(openingGap >= gc.CONFIG.SPAWN_MIN_SEPARATION - 1e-9,
        'and they are separated on the opening wave too',
        `closest opening pair: ${openingGap.toFixed(1)}`);
}

// ── 6. Flight and survivability ───────────────────────────────────────────────

console.log('\nflight');
{
    const g = makeGame(31);
    const C = g.CONFIG;

    // The ship must be able to cross the spawn range faster than a contact
    // crosses the rail, or some spawns are unreachable by construction.
    const g2 = makeGame(32);
    let frames = 0;
    g2.player.x = -C.SPAWN_X_RANGE;
    while (g2.player.x < C.SPAWN_X_RANGE && frames < 600) {
        g2.player.update(1, 0, false, 1);
        frames++;
    }
    const crossing = C.Z_FAR / g2.Difficulty.railSpeed(1);
    ok(frames < crossing,
        'the ship can cross the full spawn width before a contact crosses the rail',
        `${frames} frames to cross vs ${crossing.toFixed(0)} frames of rail`);

    // Walls hold under a held stick.
    const g3 = makeGame(33);
    for (let i = 0; i < 400; i++) g3.player.update(1, 1, false, 1);
    ok(Math.abs(g3.player.x) <= C.SHIP_X_RANGE + 0.001, 'the ship cannot leave the rail sideways');
    ok(g3.player.y <= C.SHIP_Y_MAX + 0.001, 'the ship cannot leave the rail vertically');

    // The gun honours its cooldown rather than firing every frame.
    const g4 = makeGame(34);
    let shots = 0;
    for (let i = 0; i < 90; i++) if (g4.player.update(0, 0, true, 1)) shots++;
    ok(shots <= Math.ceil(90 / C.SHOT_COOLDOWN) + 1 && shots > 1,
        'held fire respects the cooldown', `${shots} shots in 90 frames`);

    // i-frames actually protect.
    const g5 = makeGame(35);
    ok(g5.player.takeHit(), 'the first hit lands');
    ok(!g5.player.takeHit(), 'a second hit inside the invincibility window does not');
    ok(g5.player.lives === C.MAX_LIVES - 1, 'and only one ship was spent');
}

// ── 7. Draw paths run headless ────────────────────────────────────────────────

console.log('\ndraw');
{
    // The draw methods are the only DOM-adjacent code; running them against a
    // stub is what keeps a stray document reference from reaching a browser.
    const g = makeGame(41);
    for (let i = 0; i < 200; i++) g.step(1, 0.4, -0.2, true);
    const c = stubCtx();
    let threw = null;
    try {
        g.world.draw(c);
        g.entities.draw(c, g.world.camX, g.world.camY, 7);
        g.player.drawReticle(c, g.world.camX, g.world.camY);
        g.player.draw(c, g.world.camX, g.world.camY);
    } catch (e) {
        threw = e;
    }
    ok(!threw, 'a full frame draws without touching the DOM', threw && threw.message);
    ok(c.calls.length > 40, 'and actually drew something', `${c.calls.length} calls`);
}

// ── Summary ───────────────────────────────────────────────────────────────────

console.log(`\n${passed} passed, ${failed} failed\n`);
process.exit(failed ? 1 : 0);
