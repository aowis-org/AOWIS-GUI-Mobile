// Frame-rate-independent magnetic compass dial follower. All inputs are degrees
// clockwise from magnetic north; outputs are normalized to [0, 360).
// Pure JavaScript so identical logic runs in QML and hardware-free tests.
function normalize(degrees) {
    if (!isFinite(degrees))
        return -1;
    return ((degrees % 360) + 360) % 360;
}

function shortestDelta(from, to) {
    const start = normalize(from);
    const finish = normalize(to);
    if (start < 0 || finish < 0)
        return 0;
    return ((finish - start + 540) % 360) - 180;
}

function direction(degrees) {
    const normalized = normalize(degrees);
    if (normalized < 0)
        return "--";
    const octants = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"];
    return octants[Math.floor((normalized + 22.5) / 45) % 8];
}

function advance(current, target, frameSeconds) {
    const heading = normalize(current);
    const next = normalize(target);
    if (next < 0)
        return heading;
    if (heading < 0)
        return next;
    if (!isFinite(frameSeconds) || frameSeconds <= 0)
        return heading;

    const error = shortestDelta(heading, next);
    const magnitude = Math.abs(error);
    // Ignore sub-pixel magnetic noise rather than continuously moving the dial.
    if (magnitude < 0.08)
        return heading;

    // Small fluctuations get strong damping; intentional turns track quickly.
    // Use elapsed frame time instead of a restarted easing animation.
    const motion = Math.max(0, Math.min(1, (magnitude - 2) / 28));
    const responseSeconds = 0.16 - 0.105 * motion;
    // Don't suddenly jump after a temporarily suspended render loop.
    const dt = Math.min(frameSeconds, 0.05);
    const portion = 1 - Math.exp(-dt / responseSeconds);
    return normalize(heading + error * portion);
}
