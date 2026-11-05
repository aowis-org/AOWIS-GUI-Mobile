import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    width: 480
    height: 800
    // The C++ side shows this after RenderLab::attach() is complete.
    visible: false
    title: "AOWIS"
    color: "#102f45"
    property real viewZoom: 1.0
    property real dragX: 0
    property real dragY: 0
    property string lastTap: "Touch or click the map"

    function publishView() {
        lab.setView(viewZoom, dragX, dragY)
    }
    function zoomAround(factor, x, y) {
        const previous = viewZoom
        const next = Math.max(0.3, Math.min(3, previous * factor))
        const ratio = next / previous
        dragX = dragX * ratio + (x - width / 2) * (1 - ratio)
        dragY = dragY * ratio + (y - height / 2) * (1 - ratio)
        viewZoom = next
    }
    onViewZoomChanged: publishView()
    onDragXChanged: publishView()
    onDragYChanged: publishView()
    Component.onCompleted: publishView()

    // In baseline mode the grid is rendered by Qt Quick. In bgfx mode its
    // background and GPU geometry come from bgfx, never from this QML Canvas.
    Canvas {
        id: referenceMap
        anchors.fill: parent
        visible: !lab.bgfxMode
        renderStrategy: Canvas.Threaded
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "#102f45"
            ctx.fillRect(0, 0, width, height)
            ctx.strokeStyle = "#306078"
            ctx.lineWidth = 1
            const step = 44 * root.viewZoom
            for (let x = (root.dragX % step); x < width; x += step) {
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
            }
            for (let y = (root.dragY % step); y < height; y += step) {
                ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
            }
            ctx.strokeStyle = "#67c5a9"
            ctx.lineWidth = 3
            ctx.beginPath()
            ctx.moveTo(width * 0.1 + root.dragX, height * 0.55 + root.dragY)
            ctx.lineTo(width * 0.36 + root.dragX, height * 0.38 + root.dragY)
            ctx.lineTo(width * 0.65 + root.dragX, height * 0.53 + root.dragY)
            ctx.lineTo(width * 0.86 + root.dragX, height * 0.23 + root.dragY)
            ctx.stroke()
        }
        Connections {
            target: root
            function onViewZoomChanged() { referenceMap.requestPaint() }
            function onDragXChanged() { referenceMap.requestPaint() }
            function onDragYChanged() { referenceMap.requestPaint() }
        }
    }

    // Pointer handlers work with Android touchscreen multitouch, stylus,
    // mouse and touchpad. They share the same pan/zoom model as desktop.
    // QML overlays are later siblings, so their Buttons retain input priority.
    Item {
        id: mapArea
        anchors.fill: parent

        TapHandler {
            onTapped: function(point) {
                root.lastTap = "Map tap: " + Math.round(point.position.x) +
                               ", " + Math.round(point.position.y)
            }
        }

        DragHandler {
            id: panGesture
            target: null
            property real previousX: 0
            property real previousY: 0
            onActiveChanged: {
                previousX = 0
                previousY = 0
            }
            onActiveTranslationChanged: {
                if (!active || pinchGesture.active)
                    return
                const deltaX = activeTranslation.x - previousX
                const deltaY = activeTranslation.y - previousY
                previousX = activeTranslation.x
                previousY = activeTranslation.y
                root.dragX += deltaX
                root.dragY += deltaY
            }
        }

        PinchHandler {
            id: pinchGesture
            target: null
            property real baseZoom: 1
            property real baseX: 0
            property real baseY: 0
            property real anchorX: 0
            property real anchorY: 0
            onActiveChanged: {
                if (active) {
                    baseZoom = root.viewZoom
                    baseX = root.dragX
                    baseY = root.dragY
                    anchorX = centroid.position.x - mapArea.width / 2
                    anchorY = centroid.position.y - mapArea.height / 2
                }
            }
            function updatePinch() {
                if (!active)
                    return
                const newZoom = Math.max(0.3, Math.min(3, baseZoom * activeScale))
                const ratio = newZoom / baseZoom
                root.viewZoom = newZoom
                // Keep the feature underneath the original fingers anchored,
                // including translation of the gesture centroid.
                root.dragX = baseX * ratio + anchorX * (1 - ratio) + activeTranslation.x
                root.dragY = baseY * ratio + anchorY * (1 - ratio) + activeTranslation.y
            }
            onActiveScaleChanged: updatePinch()
            onActiveTranslationChanged: updatePinch()
        }

        WheelHandler {
            target: null
            onWheel: function(event) {
                const factor = Math.pow(1.25, event.angleDelta.y / 120)
                root.zoomAround(factor, point.position.x, point.position.y)
            }
        }
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
        height: headerLayout.implicitHeight + 24
        radius: 12
        color: "#ff142533"
        border.color: "#478292"
        RowLayout {
            id: headerLayout
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            Label { text: "AOWIS"; color: "white"; font.bold: true; font.pixelSize: 19 }
            Item { Layout.fillWidth: true }
            Label {
                text: lab.bgfxMode ? "BGFX UNDERLAY" : "QML BASELINE"
                color: "#9ce5da"
                font.pixelSize: 11
                font.bold: true
            }
        }
    }

    ColumnLayout {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 14 }
        spacing: 8
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: statPanel.implicitHeight + 24
            radius: 12
            color: "#ff152938"
            border.color: "#478292"
            ColumnLayout {
                id: statPanel
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                spacing: 6
                Label { text: lab.status; color: "#a1ebd6"; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: lab.details; color: "white"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12 }
                Label { text: "bgfx frames: " + lab.frames + "   |   " + root.lastTap; color: "#e3edee"; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12 }
                Label { text: lab.timings; color: "#b7dadf"; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12 }
                Label { text: lab.lifecycle; color: "#94bfc7"; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11 }
                Label { text: "Zoom: " + root.viewZoom.toFixed(2) + "×  |  Pan: " + Math.round(root.dragX) + ", " + Math.round(root.dragY); color: "#e3edee"; Layout.fillWidth: true; font.pixelSize: 12 }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Button { text: "−"; Layout.fillWidth: true; onClicked: root.viewZoom = Math.max(0.3, root.viewZoom / 1.25) }
            Button { text: "Center"; Layout.fillWidth: true; onClicked: { root.dragX = 0; root.dragY = 0 } }
            Button { text: "+"; Layout.fillWidth: true; onClicked: root.viewZoom = Math.min(3, root.viewZoom * 1.25) }
        }
    }
}
