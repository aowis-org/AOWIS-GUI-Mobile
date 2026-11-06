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
    onActivePageChanged: {
        compassService.setPageVisible(activePage === "compass")
        gradienterService.setPageVisible(activePage === "gradienter")
        plumbBobService.setPageVisible(activePage === "plumbbob")
    }
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

    // Location tracking starts automatically in the foreground. Only a
    // compact status pill overlays the map; synthetic geometry has no geo projection.
    Button {
        id: locationChip
        visible: root.activePage === "map"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 14
        width: Math.min(root.width - 28, Math.max(168, locationChipText.implicitWidth + 28))
        height: 44
        z: 1
        onClicked: root.openPage("settings")
        contentItem: Text {
            id: locationChipText
            color: root.uiText
            text: locationService.fresh
                  ? "Location " + (locationService.horizontalAccuracyMeters >= 0
                          ? "±" + Math.round(locationService.horizontalAccuracyMeters) + " m"
                          : "(accuracy unavailable)")
                  : locationService.status.indexOf("permission denied") !== -1
                    ? "Location: permission required"
                    : locationService.status.indexOf("No location provider") !== -1
                      ? "Location: unavailable"
                      : locationService.hasFix ? "Location: last fix" : "Location: searching"
            font.pixelSize: 13
            font.bold: true
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 11
            color: root.uiRaised
            border.color: locationService.fresh ? root.uiAccent : root.uiBorder
        }
        Accessible.name: "Location status; open Settings"
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
                        indicator: UiIcon {
                            kind: "down"
                            strokeColor: root.uiText
                            width: 22
                            height: 22
                            x: themeSelector.width - width - 14
                            y: (themeSelector.height - height) / 2
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
                        text: "Location"
                        color: root.uiAccent
                        font.bold: true
                        font.pixelSize: 19
                    }
                    Label {
                        text: "Location starts automatically while AOWIS is active. Android requests permission when needed. Your position stays on this device."
                        color: root.uiMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: locationService.status
                        color: locationService.fresh ? root.uiSuccess : root.uiMuted
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: locationService.hasFix
                        text: locationService.hasFix
                              ? "Latitude: " + locationService.latitude.toFixed(6)
                                + "°\nLongitude: " + locationService.longitude.toFixed(6) + "°"
                              : ""
                        color: root.uiText
                        font.pixelSize: 15
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: locationService.hasFix
                        text: "Horizontal accuracy: "
                            + (locationService.horizontalAccuracyMeters >= 0
                               ? "±" + locationService.horizontalAccuracyMeters.toFixed(1) + " m"
                               : "unavailable")
                            + "\nVertical accuracy: "
                            + (locationService.verticalAccuracyMeters >= 0
                               ? "±" + locationService.verticalAccuracyMeters.toFixed(1) + " m"
                               : "unavailable")
                            + "\nAltitude (provider): "
                            + (locationService.altitudeAvailable
                               ? locationService.altitudeMeters.toFixed(1) + " m"
                               : "unavailable")
                            + "\nSpeed: "
                            + (locationService.speedMetersPerSecond >= 0
                               ? (locationService.speedMetersPerSecond * 3.6).toFixed(1) + " km/h"
                               : "unavailable")
                            + "\nHeading: "
                            + (locationService.headingDegrees >= 0
                               ? locationService.headingDegrees.toFixed(0) + "°"
                               : "unavailable")
                            + "\nFix age: " + locationService.fixAgeSeconds + " s"
                            + "\nTimestamp (UTC): " + locationService.timestampUtc
                        color: root.uiMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
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

    // Separate instrument page. Qt Sensors supplies magnetic bearing; do not
    // substitute GPS course-over-ground when the phone is stationary.
    Rectangle {
        id: compassPage
        anchors.fill: parent
        visible: root.activePage === "compass"
        color: root.uiSurface

        ScrollView {
            id: compassScroll
            anchors.fill: parent
            anchors.topMargin: 76
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 10
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: compassScroll.availableWidth
                spacing: 12
                Label {
                    text: "Compass"
                    color: root.uiText
                    font.pixelSize: 26
                    font.bold: true
                    Layout.leftMargin: 4
                }
                Label {
                    text: "MAGNETIC NAVIGATION · LIVE INSTRUMENT"
                    color: root.uiMuted
                    font.pixelSize: 11
                    font.letterSpacing: 1.2
                    Layout.leftMargin: 4
                }
                CompassRose {
                    id: compassRose
                    readonly property real visualSize: Math.max(180,
                        Math.min(compassScroll.availableWidth - 8, root.height * .55, 400))
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: visualSize
                    Layout.preferredHeight: visualSize
                    darkTheme: root.darkTheme
                    hasHeading: compassService.hasHeading
                    headingDegrees: compassService.roseHeadingDegrees
                    Layout.topMargin: 8
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: compassService.hasHeading
                          ? ("000" + (Math.round(compassService.headingDegrees) % 360)).slice(-3) + "°"
                          : "—°"
                    font.pixelSize: 46
                    font.weight: Font.DemiBold
                    color: root.uiText
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: compassService.hasHeading ? compassService.direction + " · MAGNETIC" : "NO HEADING"
                    color: root.uiAccent
                    font.pixelSize: 15
                    font.bold: true
                    font.letterSpacing: 2
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    implicitHeight: statusColumn.implicitHeight + 28
                    radius: 14
                    color: root.uiRaised
                    border.color: root.uiBorder
                    ColumnLayout {
                        id: statusColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 15
                        anchors.rightMargin: 15
                        spacing: 7
                        Label {
                            Layout.fillWidth: true
                            text: compassService.status
                            color: compassService.hasHeading ? root.uiSuccess : root.uiMuted
                            font.pixelSize: 13
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            text: "Sensor: " + (compassService.sensorActive ? "active" : "inactive")
                                  + " · readings: " + compassService.sampleCount
                                  + " · sensor " + compassService.sensorRateHz.toFixed(0) + " Hz"
                                  + " · display " + compassService.displayRateHz.toFixed(0) + " Hz"
                            color: root.uiMuted
                            font.pixelSize: 11
                        }
                        Label {
                            Layout.fillWidth: true
                            text: compassService.calibrationStatus
                            color: compassService.calibrationLevel >= 0 && compassService.calibrationLevel < .35
                                   ? root.uiError : root.uiMuted
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: "Magnetic north, not true north. Hold the phone reasonably level and away from metal or magnets. If calibration is low, move it in a figure-eight pattern."
                    color: root.uiMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.topMargin: 4
                    Layout.bottomMargin: 15
                }
            }
        }
    }

    // Independent two-axis gravity inclinometer. Qt Sensors + native C++
    // filtering; no map renderer, magnetic compass, or JS animation required.
    Rectangle {
        id: gradienterPage
        anchors.fill: parent
        visible: root.activePage === "gradienter"
        color: root.uiSurface

        ScrollView {
            id: gradienterScroll
            anchors.fill: parent
            anchors.topMargin: 76
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 12
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: gradienterScroll.availableWidth
                spacing: 12
                Label {
                    text: "Gradienter"
                    color: root.uiText
                    font.pixelSize: 26
                    font.bold: true
                    Layout.leftMargin: 4
                }
                Label {
                    text: "TWO-AXIS SPIRIT LEVEL · GRAVITY"
                    color: root.uiMuted
                    font.pixelSize: 11
                    font.letterSpacing: 1.2
                    Layout.leftMargin: 4
                }
                GradienterVial {
                    id: gradienterVial
                    readonly property real visualSize: Math.max(170,
                        Math.min(gradienterScroll.availableWidth - 8, root.height * .46, 390))
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: visualSize
                    Layout.preferredHeight: visualSize
                    Layout.topMargin: 7
                    darkTheme: root.darkTheme
                    hasReading: gradienterService.hasReading
                    level: gradienterService.level
                    rollDegrees: gradienterService.rollDegrees
                    pitchDegrees: gradienterService.pitchDegrees
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: gradienterService.hasReading
                          ? gradienterService.tiltDegrees.toFixed(1) + "°"
                          : "—°"
                    font.pixelSize: 42
                    font.weight: Font.DemiBold
                    color: gradienterService.level ? root.uiSuccess : root.uiText
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: !gradienterService.hasReading ? "WAITING FOR SENSOR"
                          : gradienterService.level ? "LEVEL · WITHIN ±0.5°"
                          : "TILT FROM HORIZONTAL"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.0
                    color: gradienterService.level ? root.uiSuccess : root.uiMuted
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 86
                        radius: 13
                        color: root.uiRaised
                        border.color: root.uiBorder
                        Column {
                            anchors.centerIn: parent
                            spacing: 6
                            Label { text: "ROLL · LEFT / RIGHT"; color: root.uiMuted; font.pixelSize: 10; font.letterSpacing: 0.5 }
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: gradienterService.hasReading ?
                                      gradienterService.rollDegrees.toFixed(1) + "°" : "—°"
                                color: root.uiText
                                font.pixelSize: 25
                                font.bold: true
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 86
                        radius: 13
                        color: root.uiRaised
                        border.color: root.uiBorder
                        Column {
                            anchors.centerIn: parent
                            spacing: 6
                            Label { text: "PITCH · TOP / BOTTOM"; color: root.uiMuted; font.pixelSize: 10; font.letterSpacing: 0.5 }
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: gradienterService.hasReading ?
                                      gradienterService.pitchDegrees.toFixed(1) + "°" : "—°"
                                color: root.uiText
                                font.pixelSize: 25
                                font.bold: true
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Button {
                        text: "Zero reference"
                        Layout.fillWidth: true
                        enabled: gradienterService.hasReading
                        palette.buttonText: root.uiText
                        palette.text: root.uiText
                        background: Rectangle {
                            implicitHeight: 44
                            radius: 10
                            color: parent.down ? root.uiPressed : root.uiRaised
                            border.color: root.uiBorder
                            opacity: parent.enabled ? 1 : .5
                        }
                        onClicked: gradienterService.setReference()
                    }
                    Button {
                        text: "Reset zero"
                        Layout.fillWidth: true
                        enabled: gradienterService.referenceSet
                        palette.buttonText: root.uiText
                        palette.text: root.uiText
                        background: Rectangle {
                            implicitHeight: 44
                            radius: 10
                            color: parent.down ? root.uiPressed : root.uiRaised
                            border.color: root.uiBorder
                            opacity: parent.enabled ? 1 : .5
                        }
                        onClicked: gradienterService.resetReference()
                    }
                }
                Label {
                    text: gradienterService.referenceSet
                          ? "Relative reference active · zero is the surface you selected."
                          : "Absolute gravity level · zero means horizontal."
                    color: gradienterService.referenceSet ? root.uiAccent : root.uiMuted
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: gradienterStatus.implicitHeight + 27
                    radius: 13
                    color: root.uiRaised
                    border.color: root.uiBorder
                    Label {
                        id: gradienterStatus
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        text: gradienterService.status + " · sensor " +
                              (gradienterService.sensorActive ? "active" : "inactive") +
                              " · samples " + gradienterService.sampleCount
                        color: gradienterService.hasReading ? root.uiSuccess : root.uiMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: "Lay the phone flat to measure horizontal level. " +
                          "Motion and vibration affect accelerometers; allow readings to settle. " +
                          "Zero reference compares against the current surface (not true horizontal)."
                    color: root.uiMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.bottomMargin: 14
                }
            }
        }
    }

    // Gravity-based plumb instrument; no map renderer dependency and no
    // JavaScript sensor filtering or motion animation.
    Rectangle {
        id: plumbBobPage
        anchors.fill: parent
        visible: root.activePage === "plumbbob"
        color: root.uiSurface
        ScrollView {
            id: plumbBobScroll
            anchors.fill: parent
            anchors.topMargin: 76
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 12
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: plumbBobScroll.availableWidth
                spacing: 12
                Label {
                    text: "Plumb Bob"
                    color: root.uiText
                    font.pixelSize: 26
                    font.bold: true
                    Layout.leftMargin: 4
                }
                Label {
                    text: "EDGE ALIGNMENT · GRAVITY"
                    color: root.uiMuted
                    font.pixelSize: 11
                    font.letterSpacing: 1.2
                    Layout.leftMargin: 4
                }
                PlumbBobInstrument {
                    id: plumbInstrument
                    readonly property real visualSize: Math.max(170,
                        Math.min(plumbBobScroll.availableWidth - 8, root.height * .45, 390))
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: visualSize
                    Layout.preferredHeight: visualSize
                    Layout.topMargin: 7
                    darkTheme: root.darkTheme
                    hasReading: plumbBobService.hasReading
                    plumb: plumbBobService.plumb
                    downX: plumbBobService.downX
                    downY: plumbBobService.downY
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: plumbBobService.hasReading
                          ? plumbBobService.edgeDegrees.toFixed(1) + "°"
                          : "—°"
                    font.pixelSize: 42
                    font.weight: Font.DemiBold
                    color: plumbBobService.plumb ? root.uiSuccess : root.uiText
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: !plumbBobService.hasReading ? "WAITING FOR UPRIGHT READING"
                          : plumbBobService.plumb ? "EDGE PLUMB · WITHIN ±0.75°"
                          : "EDGE OFF VERTICAL"
                    color: plumbBobService.plumb ? root.uiSuccess : root.uiMuted
                    font.bold: true
                    font.pixelSize: 12
                    font.letterSpacing: 0.8
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: plumbStatus.implicitHeight + 28
                    radius: 13
                    color: root.uiRaised
                    border.color: root.uiBorder
                    Label {
                        id: plumbStatus
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        text: plumbBobService.status + " · sensor " +
                              (plumbBobService.sensorActive ? "active" : "inactive") +
                              " · samples " + plumbBobService.sampleCount
                        color: plumbBobService.hasReading ? root.uiSuccess : root.uiMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: "Hold the phone upright beside the edge you want to check. " +
                          "The hanging line follows gravity; the angle shows how far the " +
                          "phone's edge departs from vertical."
                    color: root.uiMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.bottomMargin: 15
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
                    indicator: UiIcon {
                        kind: "down"
                        strokeColor: root.uiText
                        width: 22
                        height: 22
                        x: workloadSelector.width - width - 14
                        y: (workloadSelector.height - height) / 2
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
                        contentItem: Item {
                            implicitWidth: 26
                            implicitHeight: 26
                            UiIcon {
                                kind: "minus"
                                strokeColor: root.uiText
                                width: 26
                                height: 26
                                anchors.centerIn: parent
                            }
                        }
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
                        contentItem: Item {
                            implicitWidth: 26
                            implicitHeight: 26
                            UiIcon {
                                kind: "plus"
                                strokeColor: root.uiText
                                width: 26
                                height: 26
                                anchors.centerIn: parent
                            }
                        }
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
                text: "Compass"
                Layout.fillWidth: true
                onClicked: root.openPage("compass")
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
                text: "Gradienter"
                Layout.fillWidth: true
                onClicked: root.openPage("gradienter")
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
                text: "Plumb Bob"
                Layout.fillWidth: true
                onClicked: root.openPage("plumbbob")
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
        contentItem: Item {
            implicitWidth: 30
            implicitHeight: 30
            UiIcon {
                kind: "menu"
                strokeColor: root.uiText
                width: 30
                height: 30
                anchors.centerIn: parent
            }
        }
        Accessible.name: "Open navigation menu"
        background: Rectangle {
            radius: 12
            color: root.uiRaised
            border.color: root.uiBorder
        }
        onClicked: navigationDrawer.open()
    }
}
