import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Window {
    id: root
    width: 480
    height: 800
    // C++ attaches the Qt/bgfx callbacks before making the window visible.
    visible: false
    title: Qt.application.name
    color: root.darkTheme ? "#102f45" : "#e3edf4"

    // Controls use explicit colors: the platform's default text color is not
    // necessarily legible on an AOWIS-colored surface. The map render theme
    // remains independent of the application chrome.
    readonly property bool darkTheme: appearanceSettings.dark
    readonly property color uiSurface: darkTheme ? "#102535" : "#f3f7fa"
    readonly property color uiRaised: darkTheme ? "#254354" : "#ffffff"
    readonly property color uiPressed: darkTheme ? "#35586a" : "#d8e7ed"
    readonly property color uiInput: darkTheme ? "#19384b" : "#ffffff"
    readonly property color uiText: darkTheme ? "#f2f8fa" : "#173040"
    readonly property color uiMuted: darkTheme ? "#b5ccd5" : "#405e6c"
    readonly property color uiPlaceholder: darkTheme ? "#9fb8c4" : "#587481"
    readonly property color uiAccent: darkTheme ? "#a3ead8" : "#006b57"
    readonly property color uiBorder: darkTheme ? "#6c91a1" : "#9fb5c0"
    readonly property color uiSelection: darkTheme ? "#346f86" : "#a8e2d4"
    readonly property color uiSelectedText: darkTheme ? "#ffffff" : "#102a37"
    readonly property color uiSuccess: darkTheme ? "#a2ebc2" : "#086b3c"
    readonly property color uiError: darkTheme ? "#ffb7ad" : "#ab2730"

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
            themeSelector.currentIndex = appearanceSettings.mode === "dark" ? 1
                                       : appearanceSettings.mode === "light" ? 2 : 0
        }
        activePage = page
    }
    // Back is in-app navigation while a drawer or secondary page is open.
    // At the map root, leave Android's native Back behavior intact.
    function navigateBack() {
        if (navigationDrawer.opened) {
            navigationDrawer.close()
        } else if (activePage !== "map") {
            openPage("map")
        }
    }

    // Qt 6.12 sends an unhandled Android Back to the background. A window-
    // level Shortcut works even when a Settings TextField owns keyboard focus.
    Shortcut {
        sequences: ["Back"]
        enabled: Qt.platform.os === "android" &&
                 (navigationDrawer.opened || root.activePage !== "map")
        onActivated: root.navigateBack()
    }
    Shortcut {
        sequence: "Escape"
        enabled: Qt.platform.os !== "android" &&
                 (navigationDrawer.opened || root.activePage !== "map")
        onActivated: root.navigateBack()
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
            ctx.fillStyle = root.darkTheme ? "#102f45" : "#e3edf4"
            ctx.fillRect(0, 0, width, height)
            ctx.strokeStyle = root.darkTheme ? "#306078" : "#a5c1d1"
            ctx.lineWidth = 1
            const step = 44 * root.viewZoom
            for (let x = (root.dragX % step); x < width; x += step) {
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
            }
            for (let y = (root.dragY % step); y < height; y += step) {
                ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
            }
            ctx.strokeStyle = root.darkTheme ? "#67c5a9" : "#007d68"
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
            function onDarkThemeChanged() { referenceMap.requestPaint() }
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
        color: root.uiSurface

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 76
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 20
            spacing: 15

            Label {
                text: "Settings"
                color: root.uiText
                font.pixelSize: 26
                font.bold: true
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.uiBorder }
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
                        text: "Appearance"
                        color: root.uiAccent
                        font.bold: true
                        font.pixelSize: 19
                    }
                    Label {
                        text: "Theme"
                        color: root.uiText
                        font.pixelSize: 14
                    }
                    ComboBox {
                        id: themeSelector
                        Layout.fillWidth: true
                        model: ["Follow system", "Dark", "Light"]
                        onActivated: function(index) {
                            appearanceSettings.setMode(index === 1 ? "dark"
                                                       : index === 2 ? "light" : "system")
                        }
                        palette.text: root.uiText
                        palette.buttonText: root.uiText
                        palette.base: root.uiInput
                        palette.window: root.uiInput
                        palette.highlight: root.uiSelection
                        palette.highlightedText: root.uiSelectedText
                        indicator: Text {
                            x: themeSelector.width - width - 14
                            y: (themeSelector.height - height) / 2
                            text: "▾"
                            color: root.uiText
                            font.pixelSize: 18
                        }
                        contentItem: Text {
                            text: themeSelector.displayText
                            color: root.uiText
                            leftPadding: 12
                            rightPadding: 32
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        background: Rectangle {
                            implicitHeight: 48
                            radius: 10
                            color: root.uiInput
                            border.color: themeSelector.activeFocus ? root.uiAccent : root.uiBorder
                        }
                        delegate: ItemDelegate {
                            width: themeSelector.width
                            text: modelData
                            highlighted: themeSelector.highlightedIndex === index
                            palette.text: root.uiText
                            palette.highlightedText: root.uiSelectedText
                            background: Rectangle {
                                color: parent.highlighted ? root.uiSelection : root.uiInput
                            }
                        }
                        popup: Popup {
                            y: themeSelector.height - 1
                            width: themeSelector.width
                            implicitHeight: Math.min(contentItem.implicitHeight + 4, 240)
                            padding: 2
                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: themeSelector.popup.visible ? themeSelector.delegateModel : null
                                currentIndex: themeSelector.highlightedIndex
                                ScrollIndicator.vertical: ScrollIndicator { }
                            }
                            background: Rectangle {
                                radius: 10
                                color: root.uiInput
                                border.color: root.uiBorder
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.uiBorder }
                    Label {
                        text: "Map server"
                        color: root.uiAccent
                        font.bold: true
                        font.pixelSize: 19
                    }
                    Label {
                        text: "Server URL"
                        color: root.uiText
                        font.pixelSize: 14
                    }
                    TextField {
                        id: mapServerUrl
                        color: root.uiText
                        placeholderTextColor: root.uiPlaceholder
                        selectionColor: root.uiSelection
                        selectedTextColor: root.uiSelectedText
                        background: Rectangle {
                            implicitHeight: 48
                            radius: 10
                            color: root.uiInput
                            border.color: mapServerUrl.activeFocus ? root.uiAccent : root.uiBorder
                        }
                        Layout.fillWidth: true
                        placeholderText: "http://192.168.2.10"
                        selectByMouse: true
                        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                        onTextEdited: root.saveFeedback = ""
                    }
                    Label {
                        text: "Use http:// or https://. Enter the port separately."
                        color: root.uiMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "Port"
                        color: root.uiText
                        font.pixelSize: 14
                    }
                    TextField {
                        id: mapServerPort
                        color: root.uiText
                        placeholderTextColor: root.uiPlaceholder
                        selectionColor: root.uiSelection
                        selectedTextColor: root.uiSelectedText
                        background: Rectangle {
                            implicitHeight: 48
                            radius: 10
                            color: root.uiInput
                            border.color: mapServerPort.activeFocus ? root.uiAccent : root.uiBorder
                        }
                        Layout.fillWidth: true
                        placeholderText: "e.g. 8080"
                        selectByMouse: true
                        maximumLength: 5
                        inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhNoPredictiveText
                        validator: IntValidator { bottom: 1; top: 65535 }
                        onTextEdited: root.saveFeedback = ""
                    }
                    Button {
                        palette.buttonText: root.uiText
                        palette.text: root.uiText
                        background: Rectangle {
                            implicitHeight: 44
                            radius: 10
                            color: parent.down ? root.uiPressed : root.uiRaised
                            border.color: parent.activeFocus ? root.uiAccent : root.uiBorder
                            opacity: parent.enabled ? 1.0 : 0.55
                        }
                        
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
                        color: root.saveSucceeded ? root.uiSuccess : root.uiError
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: mapServerSettings.configured
                              ? "Saved endpoint: " + mapServerSettings.endpointUrl
                              : "No map server configured yet."
                        color: root.uiText
                        font.pixelSize: 13
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "The endpoint is saved locally. Map-server requests are not implemented yet; the visible network is still synthetic test geometry."
                        color: root.uiMuted
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
        color: root.uiSurface

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
                Label { text: "Diagnostics"; color: root.uiText; font.pixelSize: 26; font.bold: true }
                Label { text: lab.bgfxMode ? "BGFX renderer" : "QML baseline"; color: root.uiAccent; font.pixelSize: 13 }
                ComboBox {
                    id: workloadSelector
                    palette.text: root.uiText
                    palette.buttonText: root.uiText
                    palette.base: root.uiInput
                    palette.window: root.uiInput
                    palette.highlight: root.uiSelection
                    palette.highlightedText: root.uiSelectedText
                    indicator: Text {
                        x: workloadSelector.width - width - 14
                        y: (workloadSelector.height - height) / 2
                        text: "▾"
                        color: root.uiText
                        font.pixelSize: 18
                    }
                    contentItem: Text {
                        text: workloadSelector.displayText
                        color: root.uiText
                        leftPadding: 12
                        rightPadding: 32
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        implicitHeight: 48
                        radius: 10
                        color: root.uiInput
                        border.color: workloadSelector.activeFocus ? root.uiAccent : root.uiBorder
                    }
                    delegate: ItemDelegate {
                        width: workloadSelector.width
                        text: modelData
                        highlighted: workloadSelector.highlightedIndex === index
                        palette.text: root.uiText
                        palette.highlightedText: root.uiSelectedText
                        background: Rectangle {
                            color: parent.highlighted ? root.uiSelection : root.uiInput
                        }
                    }
                    popup: Popup {
                        y: workloadSelector.height - 1
                        width: workloadSelector.width
                        implicitHeight: Math.min(contentItem.implicitHeight + 4, 270)
                        padding: 2
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: workloadSelector.popup.visible ? workloadSelector.delegateModel : null
                            currentIndex: workloadSelector.highlightedIndex
                            ScrollIndicator.vertical: ScrollIndicator { }
                        }
                        background: Rectangle {
                            radius: 10
                            color: root.uiInput
                            border.color: root.uiBorder
                        }
                    }
                    Layout.fillWidth: true
                    enabled: lab.bgfxMode && !lab.benchmarkActive
                    model: ["Demo", "~1k features", "~10k features", "~100k features", "~1M features"]
                    onActivated: function(index) { lab.setWorkload(index) }
                }
                Button {
                    palette.buttonText: root.uiText
                    palette.text: root.uiText
                    background: Rectangle {
                        implicitHeight: 44
                        radius: 10
                        color: parent.down ? root.uiPressed : root.uiRaised
                        border.color: parent.activeFocus ? root.uiAccent : root.uiBorder
                        opacity: parent.enabled ? 1.0 : 0.55
                    }
                    
                    text: lab.benchmarkActive ? "Recording…" : "Record 15s"
                    Layout.fillWidth: true
                    enabled: lab.bgfxMode && !lab.benchmarkActive
                    onClicked: lab.startBenchmark()
                }
                Button {
                    palette.buttonText: root.uiText
                    palette.text: root.uiText
                    background: Rectangle {
                        implicitHeight: 44
                        radius: 10
                        color: parent.down ? root.uiPressed : root.uiRaised
                        border.color: parent.activeFocus ? root.uiAccent : root.uiBorder
                        opacity: parent.enabled ? 1.0 : 0.55
                    }
                    
                    text: "Recreate GPU resources (test)"
                    Layout.fillWidth: true
                    enabled: lab.bgfxMode && !lab.benchmarkActive
                    onClicked: lab.requestGpuRecreation()
                }
                Label { text: lab.status; color: root.uiAccent; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: lab.details; color: root.uiText; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12 }
                Label { text: "bgfx frames: " + lab.frames + " | " + root.lastTap; color: root.uiText; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12 }
                Label { text: lab.timings; color: root.uiMuted; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12 }
                Label { text: lab.recoveryStatus; color: root.uiAccent; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; font.pixelSize: 11 }
                Label { text: lab.lifecycle; color: root.uiMuted; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11 }
                Label { text: lab.sceneStats; color: root.uiAccent; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11 }
                Label { text: lab.benchmarkResult; color: root.uiSuccess; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; font.pixelSize: 11; visible: lab.benchmarkResult !== "" }
                Label {
                    text: "Zoom: " + root.viewZoom.toFixed(2) + "× | Pan: " + Math.round(root.dragX) + ", " + Math.round(root.dragY)
                    color: root.uiText; Layout.fillWidth: true; font.pixelSize: 12
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        palette.buttonText: root.uiText
                        palette.text: root.uiText
                        background: Rectangle {
                            implicitHeight: 44
                            radius: 10
                            color: parent.down ? root.uiPressed : root.uiRaised
                            border.color: parent.activeFocus ? root.uiAccent : root.uiBorder
                            opacity: parent.enabled ? 1.0 : 0.55
                        }
                        text: "−"
                        Layout.fillWidth: true
                        onClicked: root.viewZoom = Math.max(0.3, root.viewZoom / 1.25)
                    }
                    Button {
                        palette.buttonText: root.uiText
                        palette.text: root.uiText
                        background: Rectangle {
                            implicitHeight: 44
                            radius: 10
                            color: parent.down ? root.uiPressed : root.uiRaised
                            border.color: parent.activeFocus ? root.uiAccent : root.uiBorder
                            opacity: parent.enabled ? 1.0 : 0.55
                        }
                        text: "Center"
                        Layout.fillWidth: true
                        onClicked: { root.dragX = 0; root.dragY = 0 }
                    }
                    Button {
                        palette.buttonText: root.uiText
                        palette.text: root.uiText
                        background: Rectangle {
                            implicitHeight: 44
                            radius: 10
                            color: parent.down ? root.uiPressed : root.uiRaised
                            border.color: parent.activeFocus ? root.uiAccent : root.uiBorder
                            opacity: parent.enabled ? 1.0 : 0.55
                        }
                        text: "+"
                        Layout.fillWidth: true
                        onClicked: root.viewZoom = Math.min(3, root.viewZoom * 1.25)
                    }
                }
            }
        }
    }

    Drawer {
        id: navigationDrawer
        width: Math.min(root.width * 0.82, 310)
        height: root.height
        edge: Qt.RightEdge
        modal: true
        // Back/Escape go through navigateBack(), in the same order everywhere.
        closePolicy: Popup.CloseOnReleaseOutside
        background: Rectangle {
            color: root.uiSurface
            border.color: root.uiBorder
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12
            Label { text: "AOWIS"; color: root.uiText; font.bold: true; font.pixelSize: 25; Layout.topMargin: 20 }
            Label { text: "Mobile"; color: root.uiMuted; font.pixelSize: 13 }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.uiBorder }
            Button {
                palette.buttonText: root.uiText
                palette.text: root.uiText
                background: Rectangle {
                    implicitHeight: 44
                    radius: 10
                    color: parent.down ? root.uiPressed : root.uiRaised
                    border.color: parent.activeFocus ? root.uiAccent : root.uiBorder
                    opacity: parent.enabled ? 1.0 : 0.55
                }
                text: "Map"
                Layout.fillWidth: true
                onClicked: root.openPage("map")
            }
            Button {
                palette.buttonText: root.uiText
                palette.text: root.uiText
                background: Rectangle {
                    implicitHeight: 44
                    radius: 10
                    color: parent.down ? root.uiPressed : root.uiRaised
                    border.color: parent.activeFocus ? root.uiAccent : root.uiBorder
                    opacity: parent.enabled ? 1.0 : 0.55
                }
                text: "Settings"
                Layout.fillWidth: true
                onClicked: root.openPage("settings")
            }
            Button {
                palette.buttonText: root.uiText
                palette.text: root.uiText
                background: Rectangle {
                    implicitHeight: 44
                    radius: 10
                    color: parent.down ? root.uiPressed : root.uiRaised
                    border.color: parent.activeFocus ? root.uiAccent : root.uiBorder
                    opacity: parent.enabled ? 1.0 : 0.55
                }
                text: "Diagnostics"
                Layout.fillWidth: true
                onClicked: root.openPage("diagnostics")
            }
            Item { Layout.fillHeight: true }
            Label {
                text: "AOWIS Mobile"
                Layout.fillWidth: true
                color: root.uiMuted
                font.pixelSize: 12
            }
        }
    }

    // A single unobtrusive control on the map replaces the old diagnostic
    // header and permanently expanded metrics/benchmark overlays.
    Button {
        id: hamburgerButton
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        width: 52
        height: 52
        text: "☰"
        palette.buttonText: root.uiText
        palette.text: root.uiText
        contentItem: Text {
            text: hamburgerButton.text
            font: hamburgerButton.font
            color: root.uiText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        font.pixelSize: 24
        Accessible.name: "Open navigation menu"
        background: Rectangle {
            radius: 12
            color: root.uiRaised
            border.color: root.uiBorder
        }
        onClicked: navigationDrawer.open()
    }
}
