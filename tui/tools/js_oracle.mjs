// js_oracle.mjs — dump the shipped JS transform's answers, for Python to check.
//
// The Python port in jovian/ is a transcription, and a transcription can be
// internally consistent and still wrong. tests/test_oracle.py runs this and
// compares, so the two implementations of one game are held to the same
// numbers rather than each to its own.
//
// The real web/js/config.js and web/js/projection.js are loaded into a vm
// context, exactly as web/tests/test-simulation.js does it. Nothing here is a
// reimplementation that could drift from what ships.
//
// Same method as texas-holdem-lava-dome/tui/tools/js_oracle.mjs, with one
// difference worth noting: that one has to hunt for a website checkout,
// because its web/ lives in another repo. Here web/ is a sibling directory, so
// the path is a fixed relative one and cannot go looking in the wrong place.

import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const HERE = dirname(fileURLToPath(import.meta.url));
const JS = resolve(HERE, '..', '..', 'web', 'js');

const context = vm.createContext({ Math, console });
for (const file of ['config.js', 'projection.js']) {
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

const DEPTHS = [-60, -10, 0, 1, 50, 205, 410, 600, 900, 1100, 1600, 3200];
const CAMS = [[0, 0], [60, -30], [-140, 84], [168, -70]];

const out = {
    // Constants the Python module claims to have transcribed. Compared field by
    // field, so a typo in a number shows up as that number rather than as a
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
};

for (const [cx, cy] of CAMS) {
    for (const z of DEPTHS) {
        for (const [wx, wy] of [[0, 0], [40, -20], [-150, 52]]) {
            out.point.push([wx, wy, z, cx, cy, Project.point(wx, wy, z, cx, cy)]);
        }
    }
}

process.stdout.write(JSON.stringify(out));
