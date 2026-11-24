import QtQuick

// Glyph-free interface icons. Rendered by Qt Quick rather than depending on
// the Android system font having the Unicode menu/chevron/math symbols.
Item {
    id: icon

    property string kind: "menu"
    property color strokeColor: "#ffffff"
    implicitWidth: 24
    implicitHeight: 24

    Canvas {
        id: strokes
        anchors.fill: parent
        renderTarget: Canvas.Image

        onPaint: {
            const context = getContext("2d")
            context.reset()
            const scale = Math.min(width, height)
            if (scale <= 0)
                return

            context.strokeStyle = icon.strokeColor
            context.lineWidth = Math.max(1.8, scale * 0.095)
            context.lineCap = "round"
            context.lineJoin = "round"
            context.beginPath()

            // Normalized paths keep the workspace symbols crisp at any size.
            function line(points) {
                context.moveTo(width * points[0], height * points[1])
                for (let i = 2; i < points.length; i += 2)
                    context.lineTo(width * points[i], height * points[i + 1])
            }
            function rectangle(x, y, w, h) {
                context.rect(width * x, height * y, width * w, height * h)
            }
            function circle(x, y, r) {
                context.moveTo(width * x + scale * r, height * y)
                context.arc(width * x, height * y, scale * r, 0, Math.PI * 2)
            }

            if (icon.kind === "menu") {
                for (let row = 0; row < 3; ++row) {
                    const y = height * (0.28 + row * 0.22)
                    context.moveTo(width * 0.24, y)
                    context.lineTo(width * 0.76, y)
                }
            } else if (icon.kind === "projects") {
                rectangle(0.10, 0.13, 0.80, 0.24)
                line([0.5, 0.37, 0.5, 0.52, 0.23, 0.52, 0.23, 0.64])
                line([0.5, 0.52, 0.77, 0.52, 0.77, 0.64])
                rectangle(0.07, 0.64, 0.32, 0.24)
                rectangle(0.61, 0.64, 0.32, 0.24)
            } else if (icon.kind === "dashboard") {
                rectangle(0.10, 0.10, 0.34, 0.50)
                rectangle(0.56, 0.10, 0.34, 0.30)
                rectangle(0.10, 0.72, 0.34, 0.18)
                rectangle(0.56, 0.52, 0.34, 0.38)
            } else if (icon.kind === "reservoirs") {
                line([0.10, 0.25, 0.10, 0.83, 0.90, 0.83, 0.90, 0.25])
                line([0.10, 0.47, 0.23, 0.41, 0.37, 0.47, 0.50, 0.41,
                      0.63, 0.47, 0.77, 0.41, 0.90, 0.47])
                line([0.38, 0.65, 0.62, 0.65])
            } else if (icon.kind === "tanks") {
                line([0.25, 0.15, 0.75, 0.15, 0.75, 0.53, 0.25, 0.53, 0.25, 0.15])
                line([0.25, 0.32, 0.75, 0.32])
                line([0.36, 0.53, 0.22, 0.90])
                line([0.64, 0.53, 0.78, 0.90])
                line([0.30, 0.72, 0.70, 0.72])
            } else if (icon.kind === "pumps") {
                circle(0.50, 0.50, 0.30)
                line([0.06, 0.50, 0.20, 0.50])
                line([0.80, 0.50, 0.94, 0.50])
                line([0.38, 0.31, 0.68, 0.50, 0.38, 0.69, 0.38, 0.31])
            } else if (icon.kind === "valves") {
                line([0.08, 0.50, 0.20, 0.50, 0.20, 0.23, 0.80, 0.77,
                      0.80, 0.50, 0.92, 0.50])
                line([0.20, 0.50, 0.20, 0.77, 0.80, 0.23, 0.80, 0.50])
                line([0.50, 0.50, 0.50, 0.10])
                line([0.36, 0.10, 0.64, 0.10])
            } else if (icon.kind === "junctions") {
                circle(0.50, 0.50, 0.15)
                line([0.08, 0.50, 0.35, 0.50])
                line([0.65, 0.50, 0.92, 0.50])
                line([0.50, 0.08, 0.50, 0.35])
                line([0.50, 0.65, 0.50, 0.92])
            } else if (icon.kind === "pipes") {
                line([0.12, 0.33, 0.45, 0.33, 0.45, 0.62, 0.88, 0.62])
                line([0.12, 0.53, 0.25, 0.53, 0.25, 0.82, 0.88, 0.82])
                line([0.12, 0.23, 0.12, 0.63])
                line([0.88, 0.52, 0.88, 0.92])
            } else if (icon.kind === "demandPoints") {
                circle(0.50, 0.28, 0.16)
                line([0.50, 0.44, 0.50, 0.71, 0.25, 0.71])
                line([0.50, 0.71, 0.75, 0.71])
                line([0.25, 0.60, 0.25, 0.82])
                line([0.75, 0.60, 0.75, 0.82])
            } else if (icon.kind === "customers") {
                circle(0.39, 0.29, 0.15)
                circle(0.75, 0.35, 0.12)
                line([0.12, 0.83, 0.12, 0.70, 0.23, 0.56, 0.55, 0.56,
                      0.66, 0.70, 0.66, 0.83, 0.12, 0.83])
                line([0.68, 0.59, 0.80, 0.59, 0.91, 0.72, 0.91, 0.83, 0.76, 0.83])
            } else if (icon.kind === "alarms") {
                line([0.50, 0.10, 0.93, 0.87, 0.07, 0.87, 0.50, 0.10])
                line([0.50, 0.37, 0.50, 0.58])
                circle(0.50, 0.72, 0.025)
            } else if (icon.kind === "logs") {
                rectangle(0.20, 0.10, 0.60, 0.80)
                line([0.34, 0.32, 0.66, 0.32])
                line([0.34, 0.50, 0.66, 0.50])
                line([0.34, 0.68, 0.66, 0.68])
            } else if (icon.kind === "settings") {
                circle(0.50, 0.50, 0.29)
                circle(0.50, 0.50, 0.11)
                for (let i = 0; i < 8; ++i) {
                    const angle = i * Math.PI / 4
                    line([0.50 + 0.30 * Math.cos(angle), 0.50 + 0.30 * Math.sin(angle),
                          0.50 + 0.43 * Math.cos(angle), 0.50 + 0.43 * Math.sin(angle)])
                }
            } else if (icon.kind === "map") {
                context.moveTo(width * 0.10, height * 0.27)
                context.lineTo(width * 0.36, height * 0.15)
                context.lineTo(width * 0.64, height * 0.28)
                context.lineTo(width * 0.90, height * 0.16)
                context.lineTo(width * 0.90, height * 0.74)
                context.lineTo(width * 0.64, height * 0.86)
                context.lineTo(width * 0.36, height * 0.73)
                context.lineTo(width * 0.10, height * 0.85)
                context.closePath()
                context.moveTo(width * 0.36, height * 0.15)
                context.lineTo(width * 0.36, height * 0.73)
                context.moveTo(width * 0.64, height * 0.28)
                context.lineTo(width * 0.64, height * 0.86)
            } else if (icon.kind === "down") {
                context.moveTo(width * 0.25, height * 0.40)
                context.lineTo(width * 0.50, height * 0.63)
                context.lineTo(width * 0.75, height * 0.40)
            } else if (icon.kind === "minus" || icon.kind === "plus") {
                context.moveTo(width * 0.25, height * 0.50)
                context.lineTo(width * 0.75, height * 0.50)
                if (icon.kind === "plus") {
                    context.moveTo(width * 0.50, height * 0.25)
                    context.lineTo(width * 0.50, height * 0.75)
                }
            } else if (icon.kind === "reset") {
                // Circular rewind arrow rendered as vector lines, not a font.
                context.arc(width * 0.52, height * 0.50,
                            scale * 0.28, -Math.PI * 0.60, Math.PI * 1.15, false)
                context.moveTo(width * 0.22, height * 0.30)
                context.lineTo(width * 0.24, height * 0.53)
                context.lineTo(width * 0.46, height * 0.46)
            } else if (icon.kind === "compass") {
                context.arc(width * 0.50, height * 0.50, scale * 0.31, 0, Math.PI * 2)
                context.moveTo(width * 0.50, height * 0.24)
                context.lineTo(width * 0.61, height * 0.53)
                context.lineTo(width * 0.50, height * 0.47)
                context.lineTo(width * 0.39, height * 0.53)
                context.closePath()
                context.moveTo(width * 0.50, height * 0.76)
                context.lineTo(width * 0.50, height * 0.56)
            } else if (icon.kind === "gradienter") {
                context.arc(width * 0.50, height * 0.50, scale * 0.34, 0, Math.PI * 2)
                context.moveTo(width * 0.18, height * 0.50)
                context.lineTo(width * 0.82, height * 0.50)
                context.moveTo(width * 0.50, height * 0.18)
                context.lineTo(width * 0.50, height * 0.82)
                context.moveTo(width * 0.64, height * 0.50)
                context.arc(width * 0.50, height * 0.50, scale * 0.14, 0, Math.PI * 2)
            } else if (icon.kind === "plumbbob") {
                context.moveTo(width * 0.50, height * 0.13)
                context.lineTo(width * 0.50, height * 0.56)
                context.moveTo(width * 0.39, height * 0.57)
                context.lineTo(width * 0.61, height * 0.57)
                context.lineTo(width * 0.69, height * 0.69)
                context.lineTo(width * 0.50, height * 0.88)
                context.lineTo(width * 0.31, height * 0.69)
                context.closePath()
            } else if (icon.kind === "location") {
                // Target: ring with four ticks, the map's device position.
                context.arc(width * 0.50, height * 0.50, scale * 0.22, 0, Math.PI * 2)
                context.moveTo(width * 0.50, height * 0.10)
                context.lineTo(width * 0.50, height * 0.26)
                context.moveTo(width * 0.50, height * 0.74)
                context.lineTo(width * 0.50, height * 0.90)
                context.moveTo(width * 0.10, height * 0.50)
                context.lineTo(width * 0.26, height * 0.50)
                context.moveTo(width * 0.74, height * 0.50)
                context.lineTo(width * 0.90, height * 0.50)
            } else if (icon.kind === "flashlight") {
                context.moveTo(width * 0.36, height * 0.12)
                context.lineTo(width * 0.64, height * 0.12)
                context.lineTo(width * 0.64, height * 0.27)
                context.lineTo(width * 0.59, height * 0.36)
                context.lineTo(width * 0.59, height * 0.82)
                context.lineTo(width * 0.41, height * 0.82)
                context.lineTo(width * 0.41, height * 0.36)
                context.lineTo(width * 0.36, height * 0.27)
                context.closePath()
                context.moveTo(width * 0.41, height * 0.45)
                context.lineTo(width * 0.59, height * 0.45)
            }
            context.stroke()
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
    onKindChanged: strokes.requestPaint()
    onStrokeColorChanged: strokes.requestPaint()
}
