import QtQuick
import "CompassMotion.js" as CompassMotion

// Vector-drawn magnetic compass. No font-icon packs, external assets, or
// renderer dependency. Dial texture is retained and only rotated on updates.
Item {
    id: compass
    property bool darkTheme: true
    property bool hasHeading: false
    property real headingDegrees: 0
    // Displayed bearing is advanced at the actual scene-graph frame rate.
    // It intentionally follows (rather than immediately mirroring) sensor data.
    property real displayedHeadingDegrees: 0
    property bool displayInitialized: false
    readonly property real headingError: CompassMotion.shortestDelta(displayedHeadingDegrees, headingDegrees)
    readonly property string displayDirection: hasHeading && displayInitialized
                                               ? CompassMotion.direction(displayedHeadingDegrees) : "--"

    function alignToHeading() {
        if (!hasHeading || !isFinite(headingDegrees) || headingDegrees < 0) {
            displayInitialized = false
            displayedHeadingDegrees = 0
            return
        }
        displayedHeadingDegrees = CompassMotion.normalize(headingDegrees)
        displayInitialized = true
    }

    onHasHeadingChanged: alignToHeading()
    onVisibleChanged: {
        // Returning from another page/background must not animate toward a
        // reading left over from the previous sensor session.
        if (visible)
            alignToHeading()
    }
    Component.onCompleted: alignToHeading()
    readonly property color bezelLight: darkTheme ? "#8caab6" : "#9cbbc4"
    readonly property color bezelDark: darkTheme ? "#10222f" : "#415b6a"
    readonly property color dialFace: darkTheme ? "#122b3b" : "#f5f9f8"
    readonly property color dialText: darkTheme ? "#eaf6f4" : "#193641"
    readonly property color dialMuted: darkTheme ? "#9bbdbe" : "#55777e"
    readonly property color dialAccent: darkTheme ? "#91ecd8" : "#007c70"
    readonly property color northRed: darkTheme ? "#ff867c" : "#bb3743"
    readonly property real diameter: Math.min(width, height)
    implicitWidth: 340
    implicitHeight: 340
    Accessible.role: Accessible.Indicator
    Accessible.name: hasHeading && displayInitialized ? "Magnetic heading " + Math.round(displayedHeadingDegrees) + " degrees" : "Magnetic heading unavailable"

    Canvas {
        id: bezel
        anchors.fill: parent
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const r = Math.min(width, height) / 2
            if (r <= 0) return
            ctx.translate(width / 2, height / 2)
            const metal = ctx.createLinearGradient(-r, -r, r, r)
            metal.addColorStop(0, compass.bezelLight)
            metal.addColorStop(0.19, compass.bezelDark)
            metal.addColorStop(0.47, compass.bezelLight)
            metal.addColorStop(0.8, compass.bezelDark)
            metal.addColorStop(1, compass.bezelLight)
            ctx.beginPath(); ctx.arc(0, 0, r * .99, 0, 2 * Math.PI)
            ctx.fillStyle = metal; ctx.fill()
            ctx.beginPath(); ctx.arc(0, 0, r * .955, 0, 2 * Math.PI)
            ctx.strokeStyle = compass.darkTheme ? "#1b3442" : "#fffef4"
            ctx.lineWidth = r * .015; ctx.stroke()
            ctx.beginPath(); ctx.arc(0, 0, r * .92, 0, 2 * Math.PI)
            ctx.fillStyle = compass.dialFace; ctx.fill()
            ctx.beginPath(); ctx.arc(0, 0, r * .92, 0, 2 * Math.PI)
            ctx.strokeStyle = compass.dialAccent; ctx.globalAlpha = 0.43
            ctx.lineWidth = 1.5; ctx.stroke(); ctx.globalAlpha = 1
            // A faint central instrument grid remains motionless under the dial.
            ctx.strokeStyle = compass.dialMuted; ctx.globalAlpha = .1
            ctx.lineWidth = 1
            for (let radius = .22; radius < .9; radius += .22) {
                ctx.beginPath(); ctx.arc(0, 0, r * radius, 0, 2 * Math.PI); ctx.stroke()
            }
            ctx.globalAlpha = 1
        }
    }

    Item {
        id: rotatingFace
        anchors.centerIn: parent
        width: compass.diameter * .91
        height: width
        rotation: compass.hasHeading && compass.displayInitialized
                  ? -compass.displayedHeadingDegrees : 0
        Canvas {
            id: rose
            anchors.fill: parent
            antialiasing: true
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const r = width / 2
                if (r <= 0) return
                ctx.translate(r, r)
                const rad = Math.PI / 180
                // 72 independently drawn graduation marks; major every 30°.
                for (let a = 0; a < 360; a += 5) {
                    const major = a % 30 === 0
                    const intermediate = a % 10 === 0
                    const from = r * (major ? .79 : intermediate ? .835 : .87)
                    const to = r * .925
                    const theta = a * rad - Math.PI / 2
                    ctx.beginPath()
                    ctx.moveTo(Math.cos(theta) * from, Math.sin(theta) * from)
                    ctx.lineTo(Math.cos(theta) * to, Math.sin(theta) * to)
                    ctx.strokeStyle = a === 0 ? compass.northRed : compass.dialMuted
                    ctx.lineWidth = major ? 2.6 : intermediate ? 1.5 : .8
                    ctx.stroke()
                }
                // Decorative compass rose: tapered petals, alternating colors.
                for (let a = 0; a < 8; ++a) {
                    const theta = a * 45 * rad - Math.PI / 2
                    const outer = r * (a % 2 === 0 ? .61 : .43)
                    const inner = r * .11
                    const spread = a % 2 === 0 ? .17 : .12
                    const noseX = Math.cos(theta) * outer
                    const noseY = Math.sin(theta) * outer
                    const baseX = Math.cos(theta) * inner
                    const baseY = Math.sin(theta) * inner
                    const sideX = Math.cos(theta + Math.PI / 2) * r * spread
                    const sideY = Math.sin(theta + Math.PI / 2) * r * spread
                    ctx.beginPath()
                    ctx.moveTo(noseX, noseY)
                    ctx.lineTo(baseX + sideX, baseY + sideY)
                    ctx.lineTo(baseX - sideX, baseY - sideY)
                    ctx.closePath()
                    ctx.fillStyle = a === 0 ? compass.northRed : (a % 2 === 0 ? compass.dialAccent : compass.dialMuted)
                    ctx.globalAlpha = a === 0 ? .94 : (a % 2 === 0 ? .67 : .26)
                    ctx.fill()
                    ctx.globalAlpha = .62
                    ctx.strokeStyle = compass.dialFace
                    ctx.lineWidth = 2
                    ctx.stroke()
                }
                ctx.globalAlpha = 1
                // Numeric 30° scale in a contrasting instrument style.
                ctx.font = "500 " + Math.max(10, r * .065) + "px sans-serif"
                ctx.fillStyle = compass.dialMuted
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                for (let a = 30; a < 360; a += 30) {
                    if (a % 90 === 0) continue
                    const theta = a * rad - Math.PI / 2
                    ctx.fillText(String(a), Math.cos(theta) * r * .69,
                                 Math.sin(theta) * r * .69)
                }
                // Always-available Latin cardinal glyphs, avoiding icon fonts.
                ctx.font = "700 " + Math.max(19, r * .15) + "px sans-serif"
                const cardinal = ["N", "E", "S", "W"]
                for (let a = 0; a < 4; ++a) {
                    const theta = a * 90 * rad - Math.PI / 2
                    ctx.fillStyle = a === 0 ? compass.northRed : compass.dialText
                    ctx.fillText(cardinal[a], Math.cos(theta) * r * .71,
                                 Math.sin(theta) * r * .71)
                }
                // Machined hub and inset crosshair.
                ctx.beginPath(); ctx.arc(0, 0, r * .105, 0, 2 * Math.PI)
                ctx.fillStyle = compass.darkTheme ? "#d4e3dd" : "#385967"
                ctx.fill()
                ctx.beginPath(); ctx.arc(0, 0, r * .067, 0, 2 * Math.PI)
                ctx.fillStyle = compass.dialFace; ctx.fill()
                ctx.beginPath(); ctx.arc(0, 0, r * .025, 0, 2 * Math.PI)
                ctx.fillStyle = compass.dialAccent; ctx.fill()
            }
        }
    }

    // The scene-graph's animation clock advances the dial once per rendered
    // frame, independently of the sensor's update rate (often ~20 Hz).
    // Stop requesting frames when the dial has caught up or isn't visible.
    FrameAnimation {
        running: compass.visible && compass.hasHeading && compass.displayInitialized
                 && Math.abs(compass.headingError) > 0.08
        onTriggered: {
            compass.displayedHeadingDegrees = CompassMotion.advance(
                compass.displayedHeadingDegrees, compass.headingDegrees, frameTime)
        }
    }

    // Fixed heading index at the top: the dial spins below it as you turn.
    Canvas {
        id: indexMarker
        anchors.fill: parent
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const r = Math.min(width, height) / 2
            if (r <= 0) return
            ctx.translate(width / 2, height / 2)
            ctx.beginPath()
            ctx.moveTo(0, -r * .79)
            ctx.lineTo(-r * .055, -r * .975)
            ctx.lineTo(r * .055, -r * .975)
            ctx.closePath()
            ctx.fillStyle = compass.dialAccent
            ctx.fill()
            ctx.strokeStyle = compass.dialFace
            ctx.lineWidth = 2
            ctx.stroke()
            ctx.beginPath(); ctx.arc(0, 0, r * .987, 0, 2 * Math.PI)
            ctx.strokeStyle = compass.darkTheme ? "#d4f3eb" : "#6d8990"
            ctx.lineWidth = 1
            ctx.globalAlpha = .35
            ctx.stroke()
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: compass.diameter * .91
        height: width
        radius: width / 2
        color: "transparent"
        border.width: compass.hasHeading ? 0 : 2
        border.color: compass.dialMuted
        opacity: compass.hasHeading ? 0 : .28
    }

    Connections {
        target: compass
        function onDarkThemeChanged() {
            bezel.requestPaint()
            rose.requestPaint()
            indexMarker.requestPaint()
        }
    }
}
