// js_oracle.mjs — dump the shipped JS simulation's answers, for Python to check.
//
// The Python port in jovian/ is a transcription, and a transcription can be
// internally consistent and still wrong. tests/test_oracle.py runs this and
// compares, so the two implementations of one game are held to the same
// numbers rather than each to its own.
//
// The real web/js/ modules are loaded into a vm context, exactly as
// web/tests/test-simulation.js does it. Nothing here is a reimplementation
// that could drift from what ships.
//
// Same method as texas-holdem-lava-dome/tui/tools/js_oracle.mjs, with one
// difference worth noting: that one has to hunt for a website checkout,
// because its web/ lives in another repo. Here web/ is a sibling directory, so
// the path is a fixed relative one and cannot go looking in the wrong place.
//
// ── On randomness ──
//
// Spawning consumes random numbers, so comparing it needs both sides drawing
// the SAME sequence. Rather than reimplement a PRNG in Python and hope the two
// agree bit for bit — 32-bit wraparound and Math.imul are exactly the kind of
// thing that differs subtly — the sequence is generated here, USED here, and
// emitted alongside the results. Python replays the array. Identical input is
// then a property of the transport rather than of two implementations
// happening to match.

import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const HERE = dirname(fileURLToPath(import.meta.url));
const JS = resolve(HERE, '..', '..', 'web', 'js');

// World.reset() calls Math.random() directly rather than taking an injected
// one -- a browser has no seeded run to protect -- so the context gets a Math
// whose random is swappable. Object.create keeps every other member (PI, cos,
// imul) resolving through the prototype, so only the one function is replaced
// and the host's own Math is left alone.
let randomHook = Math.random;
const scriptedMath = Object.create(Math);
scriptedMath.random = () => randomHook();

const context = vm.createContext({ Math: scriptedMath, console });
for (const file of ['config.js', 'projection.js', 'player.js', 'entities.js', 'world.js']) {
    vm.runInContext(readFileSync(join(JS, file), 'utf8'), context, { filename: file });
}

// Pulled out by evaluating the identifiers inside the context, not by reading
// them off the sandbox object: the modules declare `const CONFIG = …`, and a
// top-level `const` in a vm script never becomes a property of the context the
// way `var` does. Destructuring the sandbox yields undefined and the failure
// arrives several lines later as "cannot convert undefined to object". Same
// approach web/tests/test-simulation.js takes, for the same reason.
const CONFIG = vm.runInContext('CONFIG', context);
const Difficulty = vm.runInContext('Difficulty', context);
const Project = vm.runInContext('Project', context);
const newPlayer = () => vm.runInContext('new Player()', context);
const newEntities = () => vm.runInContext('new Entities()', context);
const newWorld = () => vm.runInContext('new World()', context);

// A seeded PRNG, not a formula.
//
// This was `((i * 37 + 11) % 100) / 100` first, on the reasoning that the
// sequence only has to be identical on both sides, not statistically good.
// That reasoning is wrong in a way worth recording: the cycle correlates
// spawn positions across waves, and over 1200 passive frames it produced
// exactly ZERO convoy locks -- so the doom timer, the rescue, and the convoy
// being lost went uncompared while the suite looked thorough. The same
// scenario under Math.random gives about five locks per thousand frames.
//
// mulberry32 is what web/tests/test-simulation.js seeds its own runs with, so
// the two suites now draw from the same generator as well as the same rules.
// Python never runs it: the array is emitted and replayed.
function mulberry32(seed) {
    let a = seed >>> 0;
    return function () {
        a |= 0; a = (a + 0x6D2B79F5) | 0;
        let t = Math.imul(a ^ (a >>> 15), 1 | a);
        t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}
const RANDOMS = [];
{
    const gen = mulberry32(20260905);
    for (let i = 0; i < 40000; i++) RANDOMS.push(gen());
}

function scripted() {
    let i = 0;
    return () => RANDOMS[i++ % RANDOMS.length];
}

const DEPTHS = [-60, -10, 0, 1, 50, 205, 410, 600, 900, 1100, 1600, 3200];
const CAMS = [[0, 0], [60, -30], [-140, 84], [168, -70]];

const out = {
    randoms: RANDOMS,

    // Constants the Python module claims to have transcribed. Compared field
    // by field, so a typo in a number shows up as that number rather than as a
    // gameplay difference nobody traces back here.
    config: Object.fromEntries(
        Object.entries(CONFIG).filter(([, v]) => typeof v === 'number'),
    ),

    scaleAt: DEPTHS.map((z) => [z, Project.scaleAt(z)]),
    depthAt: [1.0, 0.8, 0.56, 0.46, 0.32, 0.14].map((s) => [s, Project.depthAt(s)]),

    point: [],
    vanishing: CAMS.map(([cx, cy]) => [cx, cy, Project.vanishing(cx, cy)]),

    difficulty: [0, 0.25, 0.5, 0.75, 1].map((t) => [t, {
        railSpeed: Difficulty.railSpeed(t),
        spawnInterval: Difficulty.spawnInterval(t),
        aidShare: Difficulty.aidShare(t),
        aggro: Difficulty.aggro(t),
        identifyFrames: Difficulty.identifyFrames(t),
    }]),

    difficultyAt: [-500, 0, 8000, 32000, 320000].map((d) => [d, Difficulty.at(d)]),

    waveSize: [],
    spreadX: [],
    beacon: [],
    hitBox: {},
    player: [],
    spawn: [],
    shots: [],
};

for (const [cx, cy] of CAMS) {
    for (const z of DEPTHS) {
        for (const [wx, wy] of [[0, 0], [40, -20], [-150, 52]]) {
            out.point.push([wx, wy, z, cx, cy, Project.point(wx, wy, z, cx, cy)]);
        }
    }
}

// ── Difficulty helpers that consume randomness ───────────────────────
for (const t of [0, 0.5, 1]) {
    const r = scripted();
    const sizes = [];
    for (let i = 0; i < 12; i++) sizes.push(Difficulty.waveSize(t, r));
    out.waveSize.push([t, sizes]);
}

// ── Spawn geometry ───────────────────────────────────────────────────
for (const n of [1, 2, 3, 4, 8, 20]) {
    const e = newEntities();
    out.spreadX.push([n, e.spreadX(n, scripted())]);
}

// ── The transponder, frame by frame ──────────────────────────────────
{
    const e = newEntities();
    const aid = { kind: 'aid' };
    const hostile = { kind: 'hostile' };
    for (let f = 0; f <= 90; f++) {
        out.beacon.push([f, e.beaconLit(aid, f), e.beaconLit(hostile, f)]);
    }
    out.hitBox = { aid: e.hitBox(aid), hostile: e.hitBox(hostile) };
}

// ── The ship, flown across a scripted input space ────────────────────
//
// Deliberately includes both walls, a diagonal, a full stop and held fire, so
// clamping, drag, the bank chase and the cooldown are all exercised.
const FLIGHT = [
    [-1, 0, false, 40], [0, 0, false, 20], [1, 0, false, 60],
    [0, -1, false, 40], [0, 1, false, 90], [1, 1, true, 30],
    [0, 0, true, 25], [-1, -1, false, 50], [0, 0, false, 15],
];
for (const dt of [1, 0.5, 2]) {
    const p = newPlayer();
    const frames = [];
    for (const [ax, ay, fire, count] of FLIGHT) {
        for (let i = 0; i < count; i++) {
            const fired = p.update(ax, ay, fire, dt);
            frames.push([p.x, p.y, p.vx, p.vy, p.bank, p.shootCooldown, fired]);
        }
    }
    out.player.push([dt, frames]);
}

// ── Waves, and then a whole run ──────────────────────────────────────
for (const t of [0, 0.6]) {
    const e = newEntities();
    const r = scripted();
    const waves = [];
    for (let w = 0; w < 4; w++) {
        e.spawnWave(t, r);
        waves.push(e.contacts.map((c) => [
            c.id, c.kind, c.x, c.y, c.z, c.phase, c.driftSeed, c.aggro,
        ]));
    }
    out.spawn.push([t, waves]);
}

// A run driven end to end: spawning, drift, aggro, locks, shots, deaths.
// Everything the port has to agree about, in the order it actually happens.
//
// Flown by an aim-bot rather than a fixed input pattern. A scripted stick
// wanders the rail and hits almost nothing, so the first version of this
// produced only sightings and escorts -- and left friendly-fire attribution,
// the one thing the game most needs to get right, entirely unexercised. This
// one chases the nearest contact and shoots whatever it reaches, which kills
// convoys as readily as hostiles. That is the point: an oracle should cover
// the cruel path, not the polite one.
{
    const e = newEntities();
    const p = newPlayer();
    const r = scripted();
    const dt = 1;
    const frames = [];
    for (let f = 0; f < 900; f++) {
        const t = Difficulty.at(f * 5);
        let axisX = 0, axisY = 0;
        let nearest = null;
        for (const c of e.contacts) {
            if (!c.dead && (!nearest || c.z < nearest.z)) nearest = c;
        }
        if (nearest) {
            axisX = Math.max(-1, Math.min(1, (nearest.x - p.x) / 30));
            axisY = Math.max(-1, Math.min(1, (nearest.y - p.y) / 30));
        }
        if (p.update(axisX, axisY, true, dt)) e.fire(p);
        e.update(p, Difficulty.railSpeed(t), t, dt, r);
        const ev = e.drainEvents();
        frames.push([
            f,
            e.contacts.map((c) => [c.id, c.kind, c.x, c.y, c.z, c.doomTimer, c.lockedBy]),
            e.shots.map((s) => [s.x, s.y, s.z]),
            ev.map((x) => [x.type, x.contact.id]),
            e.particles.length,
        ]);
    }
    out.shots = frames;
}

// A second run with the guns cold.
//
// The aim-bot above kills hostiles before they ever settle on a convoy, so it
// never produces a lock -- and the doom timer, the rescue when the attacker
// dies, and the convoy actually being lost are the mechanic that makes
// escorting an active job rather than passive restraint. Holding fire is the
// only way to see them, which is a pleasing thing to have to do in this game
// in particular.
{
    const e = newEntities();
    const p = newPlayer();
    const r = scripted();
    const dt = 1;
    const frames = [];
    for (let f = 0; f < 1200; f++) {
        const t = Difficulty.at(f * 5);
        p.update(0, 0, false, dt);
        e.update(p, Difficulty.railSpeed(t), t, dt, r);
        const ev = e.drainEvents();
        frames.push([
            f,
            e.contacts.map((c) => [c.id, c.kind, c.x, c.y, c.z, c.doomTimer, c.lockedBy]),
            [],
            ev.map((x) => [x.type, x.contact.id]),
            e.particles.length,
        ]);
    }
    out.passive = frames;
}

// The lock, built rather than waited for.
//
// Even with a good generator, whether a run happens to produce a lock is luck,
// and a rule this important should not be covered by luck. Here an aggressive
// hostile is placed beside a convoy at the same depth and the pair is stepped:
// the lock forms, the doom timer runs, and the convoy is lost. The second pass
// kills the attacker partway through, which is the rescue -- the branch that
// clears the lock and is the whole reason escorting is an active job.
out.lock = [];
for (const killAttacker of [false, true]) {
    const e = newEntities();
    const p = newPlayer();
    e.contacts.push(
        { id: 1, kind: 'hostile', x: 10, y: 0, z: 700, phase: 0, driftSeed: 1,
          aggro: true, doomTimer: 0, lockedBy: 0, dead: false, pinged: false, age: 0 },
        { id: 2, kind: 'aid', x: 0, y: 0, z: 700, phase: 0, driftSeed: 1,
          aggro: false, doomTimer: 0, lockedBy: 0, dead: false, pinged: false, age: 0 },
    );
    e.nextId = 3;
    e.spawnTimer = 1e9;   // no spawning; this scenario is only the pair
    const frames = [];
    for (let f = 0; f < 200; f++) {
        if (killAttacker && f === 60) {
            const h = e.contacts.find((c) => c.id === 1);
            if (h) h.dead = true;
        }
        e.update(p, 0, 0, 1, scripted());
        frames.push([
            f,
            e.contacts.map((c) => [c.id, c.kind, c.x, c.z, c.doomTimer, c.lockedBy]),
            e.drainEvents().map((x) => [x.type, x.contact.id]),
        ]);
    }
    out.lock.push([killAttacker, frames]);
}

// ── The rail ─────────────────────────────────────────────────────────
{
    randomHook = scripted();
    const w = newWorld();
    randomHook = Math.random;   // put it back; nothing else should be scripted

    out.world = {
        bands: w.bands.slice(),
        stars: w.stars.map((s) => [s.x, s.y, s.b]),
        runs: [],
    };

    // The camera chase raised to dt, which is the same shape of bug as the
    // ship's drag: a straight multiply overshoots at low frame rates and the
    // horizon wobbles. Three timesteps, so a port that writes `* dt` fails.
    for (const dt of [1, 0.5, 2]) {
        randomHook = scripted();
        const world = newWorld();
        randomHook = Math.random;
        const p = newPlayer();
        const frames = [];
        for (let f = 0; f < 300; f++) {
            const t = Difficulty.at(world.distance);
            p.update(Math.sin(f / 30), Math.cos(f / 45), false, dt);
            world.update(p, Difficulty.railSpeed(t), dt);
            frames.push([world.distance, world.camX, world.camY, world.bands.slice()]);
        }
        out.world.runs.push([dt, frames]);
    }
}

process.stdout.write(JSON.stringify(out));
