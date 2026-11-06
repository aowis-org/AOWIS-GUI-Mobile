#!/usr/bin/env node
"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

// Test the *actual* QML motion functions, not a separately reimplemented filter.
const file = path.resolve(__dirname, "../qml/CompassMotion.js");
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(file, "utf8"), context, { filename: file });
const { normalize, shortestDelta, direction, advance } = context;

function near(actual, expected, tolerance = 1e-5) {
    assert.ok(Math.abs(actual - expected) <= tolerance,
        `expected ${actual} near ${expected} (tolerance ${tolerance})`);
}

near(normalize(-1), 359);
near(normalize(360), 0);
near(normalize(720.4), 0.4);
near(normalize(Infinity), -1);
near(shortestDelta(359, 1), 2);
near(shortestDelta(1, 359), -2);
near(shortestDelta(350, 10), 20);
near(shortestDelta(10, 350), -20);
assert.equal(direction(359), "N");
assert.equal(direction(44), "NE");
assert.equal(direction(90), "E");
assert.equal(direction(NaN), "--");

// 359 -> 1 degrees must travel 2 degrees, not spin the other way 358 degrees.
let visual = 359;
for (let i = 0; i < 120; ++i) {
    const next = advance(visual, 1, 1 / 60);
    assert.ok(shortestDelta(visual, next) >= -1e-9, "wrong turn near north");
    assert.ok(Math.abs(shortestDelta(visual, next)) < 2, "overshot north");
    visual = next;
}
assert.ok(Math.abs(shortestDelta(visual, 1)) < 0.08, "did not settle near north");

visual = 1;
for (let i = 0; i < 120; ++i) {
    const next = advance(visual, 359, 1 / 60);
    assert.ok(shortestDelta(visual, next) <= 1e-9, "wrong reverse turn near north");
    visual = next;
}
assert.ok(Math.abs(shortestDelta(visual, 359)) < 0.08);

// A large physical turn must be responsive, not get stuck on a long ease-out.
visual = 15;
for (let i = 0; i < 15; ++i)
    visual = advance(visual, 105, 1 / 60); // ~250 ms
assert.ok(Math.abs(shortestDelta(visual, 105)) < 9, "large turn too sluggish");

// After one second, equivalent 60 Hz and 120 Hz displays should agree.
function converge(hz) {
    let value = 30;
    for (let i = 0; i < hz; ++i)
        value = advance(value, 45, 1 / hz);
    return value;
}
near(converge(60), converge(120), 0.1);

// Alternating real sensor jitter at ~20 Hz should produce sub-degree motion.
visual = 0;
let maxNoise = 0;
for (let i = 0; i < 600; ++i) {
    const sample = Math.floor(i / 3) % 2 === 0 ? 0.8 : 359.2;
    visual = advance(visual, sample, 1 / 60);
    maxNoise = Math.max(maxNoise, Math.abs(shortestDelta(0, visual)));
}
assert.ok(maxNoise < 0.65, `jitter not damped (${maxNoise} degrees)`);

// Paused frames or bad samples must not move the dial; frame stalls are capped.
near(advance(22, 60, 0), 22);
near(advance(22, 60, -1), 22);
near(advance(22, NaN, 0.016), 22);
near(advance(22, 60, 15), advance(22, 60, 0.05));
near(advance(NaN, 78, 0.016), 78);

console.log("PASS: magnetic compass frame-synchronized angular follower");
