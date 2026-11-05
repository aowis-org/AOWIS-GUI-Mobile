import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    width: 480
    height: 800
    // C++ attaches the Qt/bgfx callbacks before making the window visible.
    visible: false
    title: Qt.application.name
    color: "#102f45"

    property real viewZoom: 1.0
    property real dragX: 0
    property real dragY: 0
    property string lastTap: "Touch or click the map"
    property string activePage: "map"
    property string saveFeedback: ""
    property bool saveSucceeded: false

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
    function openPage(page) {
        navigationDrawer.close()
        if (page === "settings") {
            mapServerUrl.text = mapServerSettings.serverUrl
            mapServerPort.text = mapServerSettings.serverPort > 0
                               ? String(mapServerSettings.serverPort) : ""
            saveFeedback = ""
        }
        activePage = page
    }
    onViewZoomChanged: publishView()
    onDragXChanged: publishView()
    onDragYChanged: publishView()
    Component.onCompleted: publishView()

    // Keep the QML baseline for validation. In normal mode bgfx renders
    // directly underneath these Qt Quick controls.
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

    // Gestures are disabled whenever the hamburger drawer or another page
    // is open, so they cannot accidentally pan the map behind a text field.
    Item {
        id: mapArea
        anchors.fill: parent
        enabled: root.activePage === "map" && !navigationDrawer.opened

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

    // Settings is a full page instead of a permanent panel over the map.
    Rectangle {
        id: settingsPage
        anchors.fill: parent
        visible: root.activePage === "settings"
        color: "#102535"

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 76
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 20
            spacing: 15

            Label {
                text: "Settings"
                color: "#f3f8fa"
                font.pixelSize: 26
                font.bold: true
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#416a7b" }
            ScrollView {
                id: settingsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: settingsScroll.availableWidth
                    spacing: 12
                    Label {
                        text: "Map server"
                        color: "#a5e7d4"
                        font.bold: true
                        font.pixelSize: 19
                    }
                    Label {
                        text: "Server URL"
                        color: "#dce8ec"
                        font.pixelSize: 14
                    }
                    TextField {
                        id: mapServerUrl
                        Layout.fillWidth: true
                        placeholderText: "http://192.168.2.10"
                        selectByMouse: true
                        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                        onTextEdited: root.saveFeedback = ""
                    }
                    Label {
                        text: "Use http:// or https://. Enter the port separately."
                        color: "#a4bbc4"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "Port"
                        color: "#dce8ec"
                        font.pixelSize: 14
                    }
                    TextField {
                        id: mapServerPort
                        Layout.fillWidth: true
                        placeholderText: "e.g. 8080"
                        selectByMouse: true
                        maximumLength: 5
                        inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhNoPredictiveText
                        validator: IntValidator { bottom: 1; top: 65535 }
                        onTextEdited: root.saveFeedback = ""
                    }
                    Button {
                        text: "Save map server"
                        Layout.fillWidth: true
                        enabled: mapServerUrl.text.trim().length > 0 && mapServerPort.acceptableInput
                        onClicked: {
                            root.saveSucceeded = mapServerSettings.save(
                                        mapServerUrl.text, mapServerPort.text)
                            root.saveFeedback = root.saveSucceeded
                                              ? "Map server saved on this device."
                                              : mapServerSettings.error
                            if (root.saveSucceeded) {
                                mapServerUrl.text = mapServerSettings.serverUrl
                                mapServerPort.text = String(mapServerSettings.serverPort)
                                mapServerUrl.focus = false
                                mapServerPort.focus = false
                            }
                        }
                    }
                    Label {
                        text: root.saveFeedback
                        visible: text.length > 0
                        color: root.saveSucceeded ? "#9de6bd" : "#ffb8ad"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: mapServerSettings.configured
                              ? "Saved endpoint: " + mapServerSettings.endpointUrl
                              : "No map server configured yet."
                        color: "#c5e2e7"
                        font.pixelSize: 13
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "The endpoint is saved locally. Map-server requests are not implemented yet; the visible network is still synthetic test geometry."
                        color: "#92b2be"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }

    // All prototype instrumentation lives here, never above the normal map.
    Rectangle {
        id: diagnosticsPage
        anchors.fill: parent
        visible: root.activePage === "diagnostics"
        color: "#102535"

        Flickable {
            id: diagnosticScroll
            anchors.fill: parent
            anchors.topMargin: 76
            anchors.bottomMargin: 16
            contentWidth: width
            contentHeight: diagnosticColumn.implicitHeight + 32
            flickableDirection: Flickable.VerticalFlick
            clip: true
            ColumnLayout {
                id: diagnosticColumn
                x: 16
                width: Math.max(0, diagnosticScroll.width - 32)
                spacing: 12
                Label { text: "Diagnostics"; color: "#f3f8fa"; font.pixelSize: 26; font.bold: true }
                Label { text: lab.bgfxMode ? "BGFX renderer" : "QML baseline"; color: "#a5e7d4"; font.pixelSize: 13 }
                ComboBox {
                    id: workloadSelector
                    Layout.fillWidth: true
                    enabled: lab.bgfxMode && !lab.benchmarkActive
                    model: ["Demo", "~1k features", "~10k features", "~100k features", "~1M features"]
                    onActivated: function(index) { lab.setWorkload(index) }
                }
                Button {
                    text: lab.benchmarkActive ? "Recording…" : "Record 15s"
                    Layout.fillWidth: true
                    enabled: lab.bgfxMode && !lab.benchmarkActive
                    onClicked: lab.startBenchmark()
                }
                Button {
                    text: "Recreate GPU resources (test)"
                    Layout.fillWidth: true
                    enabled: lab.bgfxMode && !lab.benchmarkActive
                    onClicked: lab.requestGpuRecreation()
                }
                Label { text: lab.status; color: "#a1ebd6"; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: lab.details; color: "#f0f7f9"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12 }
                Label { text: "bgfx frames: " + lab.frames + " | " + root.lastTap; color: "#dbe9ed"; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12 }
                Label { text: lab.timings; color: "#b7dadf"; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12 }
                Label { text: lab.recoveryStatus; color: "#ffdb9e"; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; font.pixelSize: 11 }
                Label { text: lab.lifecycle; color: "#94bfc7"; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11 }
                Label { text: lab.sceneStats; color: "#e7c88c"; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11 }
                Label { text: lab.benchmarkResult; color: "#ade9c8"; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; font.pixelSize: 11; visible: lab.benchmarkResult !== "" }
                Label {
                    text: "Zoom: " + root.viewZoom.toFixed(2) + "× | Pan: " + Math.round(root.dragX) + ", " + Math.round(root.dragY)
                    color: "#e3edee"; Layout.fillWidth: true; font.pixelSize: 12
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button { text: "−"; Layout.fillWidth: true; onClicked: root.viewZoom = Math.max(0.3, root.viewZoom / 1.25) }
                    Button { text: "Center"; Layout.fillWidth: true; onClicked: { root.dragX = 0; root.dragY = 0 } }
                    Button { text: "+"; Layout.fillWidth: true; onClicked: root.viewZoom = Math.min(3, root.viewZoom * 1.25) }
                }
            }
        }
    }

    Drawer {
        id: navigationDrawer
        width: Math.min(root.width * 0.82, 310)
        height: root.height
        edge: Qt.LeftEdge
        modal: true
        background: Rectangle {
            color: "#132d3e"
            border.color: "#416a7b"
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12
            Label { text: "AOWIS"; color: "#f2faf9"; font.bold: true; font.pixelSize: 25; Layout.topMargin: 20 }
            Label { text: "Mobile"; color: "#97c6ce"; font.pixelSize: 13 }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#416a7b" }
            Button { text: "Map"; Layout.fillWidth: true; onClicked: root.openPage("map") }
            Button { text: "Settings"; Layout.fillWidth: true; onClicked: root.openPage("settings") }
            Button { text: "Diagnostics"; Layout.fillWidth: true; onClicked: root.openPage("diagnostics") }
            Item { Layout.fillHeight: true }
            Label {
                text: "AOWIS Mobile"
                Layout.fillWidth: true
                color: "#87aebb"
                font.pixelSize: 12
            }
        }
    }

    // A single unobtrusive control on the map replaces the old diagnostic
    // header and permanently expanded metrics/benchmark overlays.
    Button {
        id: hamburgerButton
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 14
        width: 52
        height: 52
        text: "☰"
        font.pixelSize: 24
        Accessible.name: "Open navigation menu"
        background: Rectangle {
            radius: 12
            color: "#df142c3a"
            border.color: "#538593"
        }
        onClicked: navigationDrawer.open()
    }
}
