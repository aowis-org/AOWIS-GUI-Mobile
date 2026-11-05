import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    width: 480
    height: 800
    visible: true
    title: "AOWIS Mobile — Renderer Lab"
    color: "#102f45"
    property real viewZoom: 1.0
    property real dragX: 0
    property real dragY: 0
    property string lastTap: "Touch or click the map"

    function publishView() {
        lab.setView(viewZoom, dragX, dragY)
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

    MouseArea {
        id: mapArea
        anchors.fill: parent
        property real startX: 0
        property real startY: 0
        onPressed: function(mouse) {
            startX = mouse.x
            startY = mouse.y
        }
        onPositionChanged: function(mouse) {
            if (pressed) {
                root.dragX += mouse.x - startX
                root.dragY += mouse.y - startY
                startX = mouse.x
                startY = mouse.y
            }
        }
        onClicked: function(mouse) {
            root.lastTap = "Map tap: " + Math.round(mouse.x) + ", " + Math.round(mouse.y)
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
