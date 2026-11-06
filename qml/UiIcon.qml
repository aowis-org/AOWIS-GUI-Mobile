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
            }
            context.stroke()
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
    onKindChanged: strokes.requestPaint()
    onStrokeColorChanged: strokes.requestPaint()
}
