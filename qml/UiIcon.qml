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

            if (icon.kind === "menu") {
                for (let row = 0; row < 3; ++row) {
                    const y = height * (0.28 + row * 0.22)
                    context.moveTo(width * 0.24, y)
                    context.lineTo(width * 0.76, y)
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
