import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import AOWIS.Map 1.0

Window {
    id: root
    width: 480
    height: 800
    // The map item connects to the render pass before the window is shown.
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

    // Main workspaces follow the desktop navigation order. Settings and
    // tools are available from the top-right menu, not the bottom strip.
    readonly property var mainTabs: [
        { page: "projects", title: qsTr("Servers and projects") },
        { page: "dashboard", title: qsTr("Dashboard") },
        { page: "map", title: qsTr("Maps") },
        { page: "reservoirs", title: qsTr("Reservoirs") },
        { page: "tanks", title: qsTr("Tanks") },
        { page: "pumps", title: qsTr("Pumps") },
        { page: "valves", title: qsTr("Valves") },
        { page: "junctions", title: qsTr("Junctions") },
        { page: "pipes", title: qsTr("Pipes") },
        { page: "demandPoints", title: qsTr("Demand Points") },
        { page: "customers", title: qsTr("Customers") },
        { page: "alarms", title: qsTr("Alarms") },
        { page: "logs", title: qsTr("Logs") }
    ]
    readonly property int mainTabIndex: mainTabs.findIndex(tab => tab.page === activeMainPage)

    // Workspace activity is independent of the map camera (2D/3D/Look around).
    // Hydraulic monitor/editor tools will use this selection when implemented.
    property string distributionMapMode: "monitor"
    readonly property string distributionMapModeTitle: distributionMapMode === "editor"
        ? qsTr("Distribution Map Editor") : qsTr("Distribution Map Monitor")

    // Keep the selected workspace independent of Settings and tool navigation.
    // Secondary pages temporarily replace the content, not the tab selection.
    property string activeMainPage: "map"
    property string secondaryPage: ""
    readonly property string activePage: secondaryPage || activeMainPage
    onActivePageChanged: {
        if (activePage !== "map" && mapView.lookAround)
            mapView.lookAround = false
        gradienterService.setPageVisible(activePage === "gradienter")
        plumbBobService.setPageVisible(activePage === "plumbbob")
    }
    readonly property bool compassNeeded: activePage === "compass" ||
                                           (activePage === "map" && mapView.lookAround)
    onCompassNeededChanged: compassService.setPageVisible(compassNeeded)
    Connections {
        target: compassService
        function onPoseChanged() {
            if (mapView.lookAround && compassService.hasOrientation) {
                mapView.setDevicePose(compassService.orientationQuaternion,
                                      compassService.orientationTimestampSeconds,
                                      compassService.relativeOrientationQuaternion,
                                      compassService.relativeTimestampSeconds,
                                      compassService.screenRotationDegrees,
                                      locationService.declinationDegrees)
                if (mapView.lookAroundVision && compassService.fieldTimestampSeconds > 0)
                    // Number(): a missing value becomes NaN ("no GPS
                    // reference") instead of a call that throws.
                    mapView.setFieldPose(compassService.fieldOrientationQuaternion,
                                         Number(compassService.fieldTimestampSeconds),
                                         Number(compassService.fieldStrength),
                                         Number(compassService.fieldDipDegrees),
                                         Number(locationService.fieldStrengthMicroTesla),
                                         Number(locationService.inclinationDegrees))
            }
        }
    }

    // WireGuard VPN returns to where it was opened from (Settings or Map server).
    property string wireguardReturnPage: "settings"
    function openPage(page) {
        mapMenu.close()
        mapModeMenu.close()
        navigationDrawer.close()
        if (page === "mapserver" && activePage !== "wireguard")
            mapServerPage.reload()
        if (page === "wireguard")
            wireguardReturnPage = activePage === "mapserver" ? "mapserver" : "settings"
        if (page === "settings" && activePage !== "units" && activePage !== "wireguard"
                && activePage !== "mapserver") {
            themeSelector.currentIndex = appearanceSettings.mode === "dark" ? 1
                                       : appearanceSettings.mode === "light" ? 2 : 0
            const languages = ["en", "fr", "ar", "de", "es"]
            languageSelector.currentIndex = languages.indexOf(languageSettings.language)
        }
        if (mainTabs.some(tab => tab.page === page)) {
            activeMainPage = page
            secondaryPage = ""
        } else {
            secondaryPage = page
        }
    }
    // Back is in-app navigation while a drawer or secondary page is open.
    // At the map root, leave Android's native Back behavior intact.
    function navigateBack() {
        if (mapModeMenu.visible) {
            mapModeMenu.close()
        } else if (mapMenu.visible) {
            mapMenu.close()
        } else if (navigationDrawer.visible) {
            navigationDrawer.close()
        } else if (activePage === "wireguard") {
            openPage(wireguardReturnPage)
        } else if (activePage === "mapserver") {
            mapServerPage.goBack()
        } else if (activePage === "units") {
            openPage("settings")
        } else if (secondaryPage !== "") {
            openPage(activeMainPage)
        } else if (activePage !== "map") {
            openPage("map")
        }
    }

    // The focused drawer handles Back/Escape using its native popup close policy.
    // Keep page shortcuts inactive while it is visible, including transitions.
    // A window-level shortcut handles secondary pages even with a focused TextField.
    Shortcut {
        sequences: ["Back"]
        enabled: Qt.platform.os === "android" &&
                 !navigationDrawer.visible &&
                 (mapMenu.visible || mapModeMenu.visible || root.activePage !== "map")
        onActivated: root.navigateBack()
    }
    Shortcut {
        sequence: "Escape"
        enabled: Qt.platform.os !== "android" &&
                 !navigationDrawer.visible &&
                 (mapMenu.visible || mapModeMenu.visible || root.activePage !== "map")
        onActivated: root.navigateBack()
    }
    function updateLocation() {
        mapView.setLocationMarker(locationService.enabled && locationService.hasFix,
            locationService.longitude, locationService.latitude, locationService.fresh,
            locationService.enabled && locationService.follow && locationService.fresh)
    }
    Connections {
        target: locationService
        function onChanged() { root.updateLocation() }
    }

    // The AOWIS map (imagery and terrain from the embedded MAP backend)
    // bgfx draws into Qt Quick's window framebuffer, so the host must fill
    // the entire window. The bottom navigation overlays the map, just like
    // the top controls; shrinking the host would offset the GL viewport.
    MapView {
        id: mapView
        Component.onCompleted: root.updateLocation()
        onRepeatedGpsReturn: gpsHintTimer.restart()
        anchors.fill: parent
        visible: root.activePage === "map"
        enabled: root.activePage === "map" && !navigationDrawer.opened
        mapSettings: mobileMapSettings
        wireguard: wireguardVpnSettings
        darkTheme: root.darkTheme
    }

    Label {
        visible: mapView.lookAround && !compassService.hasOrientation
        anchors.centerIn: mapView
        padding: 12
        width: Math.min(parent.width - 32, 380)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: compassService.status
        color: root.uiText
        background: Rectangle { radius: 8; color: root.uiRaised }
    }

    Timer {
        id: gpsHintTimer
        interval: 5000
    }
    Label {
        visible: gpsHintTimer.running && mapView.visible && locationService.follow &&
                 locationService.enabled && !mapMenu.visible && !mapModeMenu.visible && !navigationDrawer.opened
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: mainNavigation.top
        anchors.bottomMargin: 24
        width: Math.min(parent.width - 32, 380)
        padding: 12
        z: 3
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: root.uiText
        text: qsTr("To pan freely, turn off “Keep GPS marker centered” in the map menu.")
        background: Rectangle {
            radius: 10
            color: root.uiRaised
            border.color: root.uiBorder
        }
    }

    // The map server is reached only through its WireGuard tunnel, which is
    // not running: say why, over the map, and where to fix it.
    Button {
        visible: root.activePage === "map" && mapView.serverBlocked
        anchors.top: parent.top
        anchors.left: mapControlGroup.visible ? mapControlGroup.right : parent.left
        anchors.right: hamburgerButton.left
        anchors.margins: 14
        height: Math.max(52, blockedText.implicitHeight + 16)
        z: 2
        onClicked: root.openPage("wireguard")
        contentItem: Text {
            id: blockedText
            text: qsTr("%1 Tap to open WireGuard VPN.").arg(mapView.serverStatus)
            color: root.uiText
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 11
            color: root.uiRaised
            border.color: root.uiError
        }
        Accessible.name: blockedText.text
    }

    // Map settings and distribution workspace mode share a split control.
    Item {
        id: mapControlGroup
        visible: root.activePage === "map" && mapView.rendererAvailable
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.top: hamburgerButton.top
        width: 105
        height: hamburgerButton.height
        z: 1

        Rectangle {
            anchors.fill: parent
            radius: 12
            color: root.uiRaised
        }

        Row {
            anchors.fill: parent
            spacing: 0

            Button {
                id: mapMenuButton
                width: 52
                height: parent.height
                Accessible.name: qsTr("Map controls")
                contentItem: Item {
                    UiIcon {
                        kind: "map"
                        strokeColor: root.uiText
                        width: 30
                        height: 30
                        anchors.centerIn: parent
                    }
                }
                background: Rectangle {
                    radius: 12
                    color: mapMenuButton.down ? root.uiPressed : "transparent"
                    // Keep the inner edge square.
                    Rectangle {
                        anchors.right: parent.right
                        width: 12
                        height: parent.height
                        color: parent.color
                    }
                }
                onClicked: {
                    mapModeMenu.close()
                    mapMenu.open()
                }
            }

            Rectangle {
                width: 1
                height: parent.height
                color: root.uiBorder
            }

            Button {
                id: mapModeButton
                width: 52
                height: parent.height
                Accessible.name: root.distributionMapModeTitle
                contentItem: Item {
                    UiIcon {
                        kind: root.distributionMapMode === "editor" ? "distributionEditor" : "distributionMonitor"
                        strokeColor: root.uiText
                        width: 30
                        height: 30
                        anchors.centerIn: parent
                    }
                }
                background: Rectangle {
                    radius: 12
                    color: mapModeButton.down ? root.uiPressed : "transparent"
                    Rectangle {
                        anchors.left: parent.left
                        width: 12
                        height: parent.height
                        color: parent.color
                    }
                }
                onClicked: {
                    mapMenu.close()
                    mapModeMenu.open()
                }
            }
        }

        // Draw one continuous outline, rather than borders around two buttons.
        Rectangle {
            anchors.fill: parent
            radius: 12
            color: "transparent"
            border.color: root.uiBorder
            z: 1
        }
    }

    Popup {
        id: mapMenu
        parent: root.contentItem
        modal: true
        dim: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(292, root.width - 28)
        x: mapControlGroup.x
        y: mapControlGroup.y + mapControlGroup.height + 6
        padding: 10
        height: Math.min(mapOptions.implicitHeight + 2 * padding, mainNavigation.y - y - 8)
        palette.button: root.uiRaised
        palette.buttonText: root.uiText
        palette.window: root.uiSurface
        palette.windowText: root.uiText
        palette.text: root.uiText
        palette.base: root.uiInput
        palette.highlight: root.uiSelection
        background: Rectangle {
            color: root.uiSurface
            border.color: root.uiBorder
            radius: 12
        }
        contentItem: ScrollView {
            id: mapMenuScroll
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                id: mapOptions
                width: mapMenuScroll.availableWidth
                spacing: 6
                Switch {
                    text: qsTr("GPS")
                    checked: locationService.enabled
                    onToggled: locationService.enabled = checked
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: root.uiMuted
                    text: locationService.fresh
                        ? (locationService.horizontalAccuracyMeters >= 0
                           ? qsTr("Location ±%1 m").arg(Math.round(locationService.horizontalAccuracyMeters))
                           : qsTr("Location (accuracy unavailable)"))
                        : locationService.status
                }
                CheckBox {
                    visible: locationService.enabled
                    text: qsTr("Keep GPS marker centered")
                    checked: locationService.follow
                    onToggled: locationService.follow = checked
                }
                Button {
                    id: centerOnLocationButton
                    Layout.fillWidth: true
                    visible: locationService.enabled
                    enabled: locationService.hasFix && !mapView.lookAround
                    text: qsTr("Center the map on my location")
                    contentItem: RowLayout {
                        spacing: 8
                        UiIcon {
                            kind: "location"
                            strokeColor: root.uiText
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                        }
                        Label {
                            Layout.fillWidth: true
                            text: centerOnLocationButton.text
                            color: root.uiText
                            wrapMode: Text.WordWrap
                        }
                    }
                    onClicked: {
                        mapView.centerOn(locationService.longitude, locationService.latitude, 16)
                        mapMenu.close()
                    }
                }
                Label { text: qsTr("Map provider"); color: root.uiText }
                ComboBox {
                    model: mapView.providers
                    textRole: "name"
                    valueRole: "id"
                    enabled: count > 0
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Map provider")
                    currentIndex: {
                        const entries = mapView.providers
                        const selected = mapView.providerId
                        for (let index = 0; index < entries.length; ++index) {
                            if (entries[index].id === selected)
                                return index
                        }
                        return -1
                    }
                    displayText: currentIndex >= 0 ? currentText : qsTr("No provider available")
                    onActivated: mapView.providerId = currentValue
                }
                RowLayout {
                    Button {
                        text: "2D"
                        Layout.fillWidth: true
                        highlighted: !mapView.globe
                        onClicked: mapView.setGlobe(false)
                    }
                    Button {
                        text: "3D"
                        Layout.fillWidth: true
                        enabled: mapView.globeAvailable
                        highlighted: mapView.globe
                        onClicked: mapView.setGlobe(true)
                    }
                }
                Switch {
                    visible: mapView.globe && Qt.platform.os === "android"
                    text: qsTr("Look around")
                    checked: mapView.lookAround
                    onToggled: mapView.lookAround = checked
                }
                Switch {
                    visible: mapView.lookAround
                    text: qsTr("Panorama test (no map)")
                    checked: mapView.lookAroundPanorama
                    onToggled: mapView.lookAroundPanorama = checked
                }
                Switch {
                    visible: mapView.lookAround
                    text: qsTr("Camera stabilization")
                    checked: mapView.lookAroundVision
                    onToggled: mapView.lookAroundVision = checked
                }
                Label {
                    // Diagnostics line (English, like the other diagnostics).
                    visible: mapView.lookAround && mapView.lookAroundVision &&
                             mapView.lookAroundVisionStatus.length > 0
                    color: root.uiMuted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    text: mapView.lookAroundVisionStatus
                }
                Label {
                    // Diagnostics line (English, like the other diagnostics).
                    visible: mapView.lookAround && mapView.lookAroundVision &&
                             mapView.lookAroundFieldStatus.length > 0
                    color: root.uiMuted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    text: mapView.lookAroundFieldStatus
                }
                Label {
                    // Sensor stages of the own field orientation (diagnostics).
                    visible: mapView.lookAround && mapView.lookAroundVision &&
                             compassService.fieldDiagnostics.length > 0
                    color: root.uiMuted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    text: compassService.fieldDiagnostics
                }
                Label {
                    visible: mapView.lookAround
                    color: root.uiMuted
                    text: Number.isFinite(locationService.declinationDegrees)
                          ? qsTr("Geographic north") : qsTr("Magnetic north · no location correction")
                }
                RowLayout {
                    Button {
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Zoom out")
                        contentItem: Item {
                            implicitHeight: 28
                            UiIcon { kind: "minus"; strokeColor: root.uiText; anchors.centerIn: parent }
                        }
                        onClicked: mapView.zoomBy(-1)
                    }
                    Button {
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Zoom in")
                        contentItem: Item {
                            implicitHeight: 28
                            UiIcon { kind: "plus"; strokeColor: root.uiText; anchors.centerIn: parent }
                        }
                        onClicked: mapView.zoomBy(1)
                    }
                }
                CheckBox {
                    visible: mapView.globe
                    text: qsTr("Wireframe")
                    checked: mapView.wireframe
                    onToggled: mapView.wireframe = checked
                }
                CheckBox {
                    visible: mapView.globe
                    text: qsTr("Hillshade")
                    checked: mapView.hillshade
                    onToggled: mapView.hillshade = checked
                }
            }
        }
    }

    Popup {
        id: mapModeMenu
        parent: root.contentItem
        modal: true
        dim: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(330, root.width - 28)
        x: Math.max(14, Math.min(mapControlGroup.x + 53, root.width - width - 14))
        y: mapControlGroup.y + mapControlGroup.height + 6
        padding: 10
        height: Math.min(mapModeOptions.implicitHeight + 2 * padding, mainNavigation.y - y - 8)
        palette.button: root.uiRaised
        palette.buttonText: root.uiText
        palette.window: root.uiSurface
        palette.windowText: root.uiText
        palette.text: root.uiText
        palette.base: root.uiInput
        palette.highlight: root.uiSelection
        background: Rectangle {
            color: root.uiSurface
            border.color: root.uiBorder
            radius: 12
        }
        contentItem: ScrollView {
            id: mapModeScroll
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                id: mapModeOptions
                width: mapModeScroll.availableWidth
                spacing: 6
                Repeater {
                    model: [
                        { mode: "monitor", title: qsTr("Distribution Map Monitor"), icon: "distributionMonitor" },
                        { mode: "editor", title: qsTr("Distribution Map Editor"), icon: "distributionEditor" }
                    ]
                    delegate: Button {
                        id: modeChoice
                        required property var modelData
                        readonly property bool selected: root.distributionMapMode === modelData.mode
                        Layout.fillWidth: true
                        Layout.minimumHeight: 44
                        text: modelData.title
                        highlighted: selected
                        Accessible.role: Accessible.RadioButton
                        Accessible.checked: selected
                        contentItem: RowLayout {
                            spacing: 8
                            UiIcon {
                                kind: modeChoice.modelData.icon
                                strokeColor: modeChoice.selected ? root.uiSelectedText : root.uiText
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modeChoice.text
                                wrapMode: Text.WordWrap
                                color: modeChoice.selected ? root.uiSelectedText : root.uiText
                            }
                        }
                        background: Rectangle {
                            radius: 6
                            color: modeChoice.down ? root.uiPressed :
                                   modeChoice.selected ? root.uiSelection : root.uiRaised
                            border.color: modeChoice.selected ? root.uiAccent : root.uiBorder
                        }
                        onClicked: {
                            root.distributionMapMode = modelData.mode
                            mapModeMenu.close()
                        }
                    }
                }
            }
        }
    }

    Button {
        id: mapCompass
        visible: mapView.visible && mapView.globe
        anchors.left: parent.left
        anchors.bottom: mainNavigation.top
        anchors.margins: 14
        width: 76
        height: width
        z: 1
        padding: 0
        Accessible.name: qsTr("Rotate map to north")
        onClicked: mapView.faceNorth()
        contentItem: CompassRose {
            compact: true
            darkTheme: root.darkTheme
            hasHeading: true
            // The dial rotates opposite its heading input. Local north on
            // the map is clockwise from screen-up for positive globe yaw.
            headingDegrees: -mapView.northAngle
            Accessible.ignored: true
            scale: mapCompass.down ? 0.95 : 1
        }
        background: null
    }

    // Settings is a full page instead of a permanent panel over the map.
    Rectangle {
        id: settingsPage
        LayoutMirroring.enabled: languageSettings.rightToLeft
        LayoutMirroring.childrenInherit: true
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: mainNavigation.top
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
                text: qsTr("Settings")
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
                        text: qsTr("Language")
                        color: root.uiAccent
                        font.bold: true
                        font.pixelSize: 19
                    }
                    ComboBox {
                        id: languageSelector
                        Layout.fillWidth: true
                        textRole: "label"
                        model: [
                            { "code": "en", "label": "EN   English" },
                            { "code": "fr", "label": "FR   Français" },
                            { "code": "ar", "label": "AR   العربية" },
                            { "code": "de", "label": "DE   Deutsch" },
                            { "code": "es", "label": "ES   Español" }
                        ]
                        onActivated: function(index) {
                            languageSettings.setLanguage(languageSelector.model[index].code)
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
                            x: languageSettings.rightToLeft ? 14 : languageSelector.width - width - 14
                            y: (languageSelector.height - height) / 2
                        }
                        contentItem: Text {
                            text: languageSelector.displayText
                            color: root.uiText
                            leftPadding: languageSettings.rightToLeft ? 32 : 12
                            rightPadding: languageSettings.rightToLeft ? 12 : 32
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        background: Rectangle {
                            implicitHeight: 48
                            radius: 10
                            color: root.uiInput
                            border.color: languageSelector.activeFocus ? root.uiAccent : root.uiBorder
                        }
                        delegate: ItemDelegate {
                            width: languageSelector.width
                            text: modelData.label
                            highlighted: languageSelector.highlightedIndex === index
                            palette.text: root.uiText
                            palette.highlightedText: root.uiSelectedText
                            background: Rectangle {
                                color: parent.highlighted ? root.uiSelection : root.uiInput
                            }
                        }
                        popup: Popup {
                            y: languageSelector.height - 1
                            width: languageSelector.width
                            implicitHeight: Math.min(contentItem.implicitHeight + 4, 280)
                            padding: 2
                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: languageSelector.popup.visible ? languageSelector.delegateModel : null
                                currentIndex: languageSelector.highlightedIndex
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
                        text: qsTr("Units")
                        color: root.uiAccent
                        font.bold: true
                        font.pixelSize: 19
                    }
                    Label {
                        text: qsTr("Unit profile")
                        color: root.uiText
                        font.pixelSize: 14
                    }
                    ComboBox {
                        id: unitProfileSelector
                        Layout.fillWidth: true
                        model: unitProfileSettings.listProfiles(languageSettings.language, unitProfileSettings.revision)
                        textRole: "label"
                        currentIndex: unitProfileSettings.currentIndex
                        onActivated: function(index) { unitProfileSettings.selectProfile(index) }
                        palette.text: root.uiText
                        palette.buttonText: root.uiText
                        palette.base: root.uiInput
                        palette.window: root.uiInput
                        palette.highlight: root.uiSelection
                        palette.highlightedText: root.uiSelectedText
                        indicator: UiIcon {
                            kind: "down"
                            strokeColor: root.uiText
                            width: 22; height: 22
                            x: languageSettings.rightToLeft ? 14 : unitProfileSelector.width - width - 14
                            y: (unitProfileSelector.height - height) / 2
                        }
                        contentItem: Text {
                            text: unitProfileSelector.displayText
                            color: root.uiText
                            leftPadding: languageSettings.rightToLeft ? 32 : 12
                            rightPadding: languageSettings.rightToLeft ? 12 : 32
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        background: Rectangle {
                            implicitHeight: 48
                            radius: 10
                            color: root.uiInput
                            border.color: unitProfileSelector.activeFocus ? root.uiAccent : root.uiBorder
                        }
                        delegate: ItemDelegate {
                            width: unitProfileSelector.width
                            text: modelData.label
                            highlighted: unitProfileSelector.highlightedIndex === index
                            palette.text: root.uiText
                            palette.highlightedText: root.uiSelectedText
                            background: Rectangle {
                                color: parent.highlighted ? root.uiSelection : root.uiInput
                            }
                        }
                        popup: Popup {
                            y: unitProfileSelector.height - 1
                            width: unitProfileSelector.width
                            implicitHeight: Math.min(contentItem.implicitHeight + 4, 300)
                            padding: 2
                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: unitProfileSelector.popup.visible ? unitProfileSelector.delegateModel : null
                                currentIndex: unitProfileSelector.highlightedIndex
                                ScrollIndicator.vertical: ScrollIndicator { }
                            }
                            background: Rectangle {
                                radius: 10
                                color: root.uiInput
                                border.color: root.uiBorder
                            }
                        }
                    }
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("Detailed unit settings…")
                        palette.buttonText: root.uiText
                        palette.text: root.uiText
                        background: Rectangle {
                            implicitHeight: 44
                            radius: 10
                            color: parent.down ? root.uiPressed : root.uiRaised
                            border.color: parent.activeFocus ? root.uiAccent : root.uiBorder
                        }
                        onClicked: root.openPage("units")
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.uiBorder }
                    Label {
                        text: qsTr("Location")
                        color: root.uiAccent
                        font.bold: true
                        font.pixelSize: 19
                    }
                    Label {
                        text: qsTr("Location starts automatically while AOWIS is active. Android requests permission when needed. Your position stays on this device.")
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
                              ? qsTr("Latitude: %1°\nLongitude: %2°")
                                    .arg(locationService.latitude.toFixed(6))
                                    .arg(locationService.longitude.toFixed(6))
                              : ""
                        color: root.uiText
                        font.pixelSize: 15
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: locationService.hasFix
                        text: qsTr("Horizontal accuracy: %1\nVertical accuracy: %2\nAltitude (provider): %3\nSpeed: %4\nHeading: %5\nFix age: %6 s\nTimestamp (UTC): %7")
                            .arg(locationService.horizontalAccuracyMeters >= 0
                                 ? "±" + unitProfileSettings.formatDistance(locationService.horizontalAccuracyMeters, unitProfileSettings.revision)
                                 : qsTr("unavailable"))
                            .arg(locationService.verticalAccuracyMeters >= 0
                                 ? "±" + unitProfileSettings.formatDistance(locationService.verticalAccuracyMeters, unitProfileSettings.revision)
                                 : qsTr("unavailable"))
                            .arg(locationService.altitudeAvailable
                                 ? unitProfileSettings.formatAltitude(locationService.altitudeMeters, unitProfileSettings.revision)
                                 : qsTr("unavailable"))
                            .arg(locationService.speedMetersPerSecond >= 0
                                 ? unitProfileSettings.formatSpeed(locationService.speedMetersPerSecond, unitProfileSettings.revision)
                                 : qsTr("unavailable"))
                            .arg(locationService.headingDegrees >= 0
                                 ? locationService.headingDegrees.toFixed(0) + "°"
                                 : qsTr("unavailable"))
                            .arg(locationService.fixAgeSeconds)
                            .arg(locationService.timestampUtc)
                        color: root.uiMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.uiBorder }
                    Label {
                        text: qsTr("Network & servers")
                        color: root.uiAccent
                        font.bold: true
                        font.pixelSize: 19
                    }
                    Button {
                        id: mapServerButton
                        Layout.fillWidth: true
                        text: qsTr("Map settings")
                        palette.buttonText: root.uiText
                        palette.text: root.uiText
                        background: Rectangle {
                            implicitHeight: 44
                            radius: 10
                            color: mapServerButton.down ? root.uiPressed : root.uiRaised
                            border.color: mapServerButton.activeFocus ? root.uiAccent : root.uiBorder
                        }
                        onClicked: root.openPage("mapserver")
                    }
                    Label {
                        text: mobileMapSettings.upstreams.length === 0
                              ? qsTr("No upstream configured. Local map acquisition policy applies.")
                              : qsTr("MAP upstream servers") + ": " + mobileMapSettings.upstreams.length
                        color: root.uiMuted
                        font.pixelSize: 12
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                    Button {
                        id: wireguardButton
                        visible: wireguardVpnSettings.engineAvailable
                        Layout.fillWidth: true
                        text: qsTr("WireGuard VPN")
                        palette.buttonText: root.uiText
                        palette.text: root.uiText
                        background: Rectangle {
                            implicitHeight: 44
                            radius: 10
                            color: wireguardButton.down ? root.uiPressed : root.uiRaised
                            border.color: wireguardButton.activeFocus ? root.uiAccent : root.uiBorder
                        }
                        onClicked: root.openPage("wireguard")
                    }
                    Label {
                        visible: wireguardVpnSettings.engineAvailable
                        text: qsTr("Encrypted WireGuard tunnels for servers that are only reachable over a VPN. They run inside AOWIS only.")
                        color: root.uiMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.uiBorder }
                    Label {
                        text: qsTr("Appearance")
                        color: root.uiAccent
                        font.bold: true
                        font.pixelSize: 19
                    }
                    Label {
                        text: qsTr("Theme")
                        color: root.uiText
                        font.pixelSize: 14
                    }
                    ComboBox {
                        id: themeSelector
                        Layout.fillWidth: true
                        model: [qsTr("Follow system"), qsTr("Dark"), qsTr("Light")]
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
                            x: languageSettings.rightToLeft ? 14 : themeSelector.width - width - 14
                            y: (themeSelector.height - height) / 2
                        }
                        contentItem: Text {
                            text: themeSelector.displayText
                            color: root.uiText
                            leftPadding: languageSettings.rightToLeft ? 32 : 12
                            rightPadding: languageSettings.rightToLeft ? 12 : 32
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
                }
            }
        }
    }

    UnitSettingsPage {
        id: unitsPage
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: mainNavigation.top
        visible: root.activePage === "units"
        surfaceColor: root.uiSurface
        raisedColor: root.uiRaised
        inputColor: root.uiInput
        textColor: root.uiText
        mutedColor: root.uiMuted
        accentColor: root.uiAccent
        borderColor: root.uiBorder
        selectedColor: root.uiSelection
        selectedTextColor: root.uiSelectedText
        rightToLeft: languageSettings.rightToLeft
        onBackRequested: root.openPage("settings")
    }

    MapServerSettingsPage {
        id: mapServerPage
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: mainNavigation.top
        visible: root.activePage === "mapserver"
        surfaceColor: root.uiSurface
        raisedColor: root.uiRaised
        pressedColor: root.uiPressed
        inputColor: root.uiInput
        textColor: root.uiText
        mutedColor: root.uiMuted
        placeholderColor: root.uiPlaceholder
        accentColor: root.uiAccent
        borderColor: root.uiBorder
        selectedColor: root.uiSelection
        selectedTextColor: root.uiSelectedText
        successColor: root.uiSuccess
        errorColor: root.uiError
        rightToLeft: languageSettings.rightToLeft
        onBackRequested: root.openPage("settings")
        onWireguardRequested: root.openPage("wireguard")
    }

    WireGuardSettingsPage {
        id: wireguardPage
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: mainNavigation.top
        visible: root.activePage === "wireguard"
        surfaceColor: root.uiSurface
        raisedColor: root.uiRaised
        pressedColor: root.uiPressed
        inputColor: root.uiInput
        textColor: root.uiText
        mutedColor: root.uiMuted
        placeholderColor: root.uiPlaceholder
        accentColor: root.uiAccent
        borderColor: root.uiBorder
        selectedColor: root.uiSelection
        selectedTextColor: root.uiSelectedText
        successColor: root.uiSuccess
        errorColor: root.uiError
        rightToLeft: languageSettings.rightToLeft
        backText: root.wireguardReturnPage === "mapserver" ? qsTr("Back to Map settings") : qsTr("Back to Settings")
        onBackRequested: root.openPage(root.wireguardReturnPage)
    }

    // Separate instrument page. Qt Sensors supplies magnetic bearing; do not
    // substitute GPS course-over-ground when the phone is stationary.
    Rectangle {
        id: compassPage
        LayoutMirroring.enabled: languageSettings.rightToLeft
        LayoutMirroring.childrenInherit: true
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: mainNavigation.top
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
                    text: qsTr("Compass")
                    color: root.uiText
                    font.pixelSize: 26
                    font.bold: true
                    Layout.leftMargin: 4
                }
                Label {
                    text: qsTr("MAGNETIC NAVIGATION · LIVE INSTRUMENT")
                    color: root.uiMuted
                    font.pixelSize: 11
                    font.letterSpacing: 1.2
                    Layout.leftMargin: 4
                }
                CompassRose {
                    LayoutMirroring.enabled: false
                    LayoutMirroring.childrenInherit: false
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
                    text: compassService.hasHeading ? qsTr("%1 · MAGNETIC").arg(compassService.direction) : qsTr("NO HEADING")
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
                            text: qsTr("Sensor: %1 · readings: %2 · sensor %3 Hz · display %4 Hz")
                                  .arg(compassService.sensorActive ? qsTr("active") : qsTr("inactive"))
                                  .arg(compassService.sampleCount)
                                  .arg(compassService.sensorRateHz.toFixed(0))
                                  .arg(compassService.displayRateHz.toFixed(0))
                            color: root.uiMuted
                            font.pixelSize: 11
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: compassService.sourceDescription
                            color: root.uiMuted
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            text: compassService.hardwareDescription
                            color: root.uiMuted
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
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
                    text: qsTr("Magnetic north, not true north. Hold the phone reasonably level and away from metal or magnets. If calibration is low, move it in a figure-eight pattern.")
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
        LayoutMirroring.enabled: languageSettings.rightToLeft
        LayoutMirroring.childrenInherit: true
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: mainNavigation.top
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
                    text: qsTr("Gradienter")
                    color: root.uiText
                    font.pixelSize: 26
                    font.bold: true
                    Layout.leftMargin: 4
                }
                Label {
                    text: qsTr("TWO-AXIS SPIRIT LEVEL · GRAVITY")
                    color: root.uiMuted
                    font.pixelSize: 11
                    font.letterSpacing: 1.2
                    Layout.leftMargin: 4
                }
                GradienterVial {
                    LayoutMirroring.enabled: false
                    LayoutMirroring.childrenInherit: false
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
                    text: !gradienterService.hasReading ? qsTr("WAITING FOR SENSOR")
                          : gradienterService.level ? qsTr("LEVEL · WITHIN ±0.5°")
                          : qsTr("TILT FROM HORIZONTAL")
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
                            Label { text: qsTr("ROLL · LEFT / RIGHT"); color: root.uiMuted; font.pixelSize: 10; font.letterSpacing: 0.5 }
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
                            Label { text: qsTr("PITCH · TOP / BOTTOM"); color: root.uiMuted; font.pixelSize: 10; font.letterSpacing: 0.5 }
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
                        text: qsTr("Zero reference")
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
                        text: qsTr("Reset zero")
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
                          ? qsTr("Relative reference active · zero is the surface you selected.")
                          : qsTr("Absolute gravity level · zero means horizontal.")
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
                        text: qsTr("%1 · sensor %2 · samples %3")
                              .arg(gradienterService.status)
                              .arg(gradienterService.sensorActive ? qsTr("active") : qsTr("inactive"))
                              .arg(gradienterService.sampleCount)
                        color: gradienterService.hasReading ? root.uiSuccess : root.uiMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Lay the phone flat to measure horizontal level. Motion and vibration affect accelerometers; allow readings to settle. Zero reference compares against the current surface (not true horizontal).")
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
        LayoutMirroring.enabled: languageSettings.rightToLeft
        LayoutMirroring.childrenInherit: true
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: mainNavigation.top
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
                    text: qsTr("Plumb Bob")
                    color: root.uiText
                    font.pixelSize: 26
                    font.bold: true
                    Layout.leftMargin: 4
                }
                Label {
                    text: qsTr("EDGE ALIGNMENT · GRAVITY")
                    color: root.uiMuted
                    font.pixelSize: 11
                    font.letterSpacing: 1.2
                    Layout.leftMargin: 4
                }
                PlumbBobInstrument {
                    LayoutMirroring.enabled: false
                    LayoutMirroring.childrenInherit: false
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
                    text: !plumbBobService.hasReading ? qsTr("WAITING FOR UPRIGHT READING")
                          : plumbBobService.plumb ? qsTr("EDGE PLUMB · WITHIN ±0.75°")
                          : qsTr("EDGE OFF VERTICAL")
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
                        text: qsTr("%1 · sensor %2 · samples %3")
                              .arg(plumbBobService.status)
                              .arg(plumbBobService.sensorActive ? qsTr("active") : qsTr("inactive"))
                              .arg(plumbBobService.sampleCount)
                        color: plumbBobService.hasReading ? root.uiSuccess : root.uiMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Hold the phone upright beside the edge you want to check. The hanging line follows gravity; the angle shows how far the phone's edge departs from vertical.")
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
        LayoutMirroring.enabled: languageSettings.rightToLeft
        LayoutMirroring.childrenInherit: true
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: mainNavigation.top
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
                Label { text: qsTr("Diagnostics"); color: root.uiText; font.pixelSize: 26; font.bold: true }
                Label { text: qsTr("Map"); color: root.uiAccent; font.bold: true; font.pixelSize: 19 }
                Label { text: mapView.status; color: root.uiAccent; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: mapView.serverStatus; color: root.uiText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; font.pixelSize: 12 }
                Label { text: mapView.diagnostics; color: root.uiMuted; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12 }
                Label { text: mapView.lifecycle; color: root.uiMuted; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                Label {
                    text: qsTr("Center: %1, %2 | %3")
                        .arg(mapView.centerLatitude.toFixed(5)).arg(mapView.centerLongitude.toFixed(5))
                        .arg(mapView.globe ? qsTr("Globe") : qsTr("2D"))
                    color: root.uiText; Layout.fillWidth: true; font.pixelSize: 12
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
                    text: qsTr("Reload map server")
                    Layout.fillWidth: true
                    enabled: mapView.rendererAvailable
                    onClicked: mapView.reloadServer()
                }
            }
        }
    }

    // Future workspaces remain distinct, navigable pages without inventing
    // data or pretending that desktop functionality has been ported.
    Rectangle {
        id: pendingWorkspace
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: mainNavigation.top
        visible: root.secondaryPage === "" && root.activeMainPage !== "map"
        color: root.uiSurface
        LayoutMirroring.enabled: languageSettings.rightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 76
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 20
            spacing: 15
            Label {
                Layout.fillWidth: true
                text: root.mainTabIndex >= 0 ? root.mainTabs[root.mainTabIndex].title : ""
                color: root.uiText
                font.pixelSize: 26
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.uiBorder }
            Label {
                Layout.fillWidth: true
                text: qsTr("This workspace is not yet implemented on mobile.")
                color: root.uiMuted
                wrapMode: Text.WordWrap
            }
            Item { Layout.fillHeight: true }
        }
    }

    // Persistent, horizontally scrollable main tabs. Keep tap targets large
    // enough for touch rather than squeezing the desktop tab count onto a phone.
    Rectangle {
        id: mainNavigation
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 54
        color: root.uiSurface
        z: 4

        // Do not pass clicks in the strip's unused space to the map below.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: event => { event.accepted = true }
        }

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: root.uiBorder
        }
        ListView {
            id: navigationList
            anchors.fill: parent
            anchors.topMargin: 1
            orientation: ListView.Horizontal
            layoutDirection: languageSettings.rightToLeft ? Qt.RightToLeft : Qt.LeftToRight
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            model: root.mainTabs
            currentIndex: root.mainTabIndex
            onCurrentIndexChanged: {
                if (currentIndex >= 0 && currentIndex < count)
                    positionViewAtIndex(currentIndex, ListView.Contain)
            }
            Component.onCompleted: {
                if (currentIndex >= 0)
                    positionViewAtIndex(currentIndex, ListView.Contain)
            }
            delegate: Button {
                id: mainTabButton
                required property var modelData
                required property int index
                readonly property bool selected: index === root.mainTabIndex
                width: Math.max(78, tabLabel.implicitWidth + 20,
                                navigationList.width / root.mainTabs.length - navigationList.spacing)
                height: navigationList.height - navigationScrollBar.height
                padding: 2
                text: modelData.title
                highlighted: selected
                Accessible.name: text
                onClicked: root.openPage(modelData.page)
                background: Rectangle {
                    radius: 6
                    color: mainTabButton.down ? root.uiPressed :
                           mainTabButton.selected ? root.uiSelection : root.uiSurface
                    border.color: mainTabButton.activeFocus ? root.uiAccent : "transparent"
                    Rectangle {
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width - 12
                        height: 3
                        radius: 1.5
                        color: root.uiAccent
                        visible: mainTabButton.selected
                    }
                }
                contentItem: Item {
                    Column {
                        spacing: 1
                        anchors.centerIn: parent
                        UiIcon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            kind: mainTabButton.modelData.page
                            strokeColor: mainTabButton.selected && !mainTabButton.down ?
                                         root.uiSelectedText : root.uiText
                            width: 22
                            height: 22
                        }
                        Text {
                            id: tabLabel
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: mainTabButton.text
                            font.pixelSize: 12
                            font.bold: mainTabButton.selected
                            color: mainTabButton.selected && !mainTabButton.down ?
                                   root.uiSelectedText : root.uiText
                        }
                    }
                }
            }
            ScrollBar.horizontal: ScrollBar {
                id: navigationScrollBar
                policy: ScrollBar.AlwaysOn
                height: 3
                padding: 0
                contentItem: Rectangle {
                    radius: 2
                    color: root.uiBorder
                    opacity: navigationList.contentWidth > navigationList.width ? 1 : 0
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
        // Give the modal drawer focus so Qt handles Android Back and Escape.
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside
        background: Rectangle {
            color: root.uiSurface
            border.color: root.uiBorder
        }
        ColumnLayout {
            LayoutMirroring.enabled: languageSettings.rightToLeft
            LayoutMirroring.childrenInherit: true
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12
            Label { text: qsTr("AOWIS"); color: root.uiText; font.bold: true; font.pixelSize: 25; Layout.topMargin: 20 }
            Label { text: qsTr("Mobile"); color: root.uiMuted; font.pixelSize: 13 }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.uiBorder }
            Button {
                palette.buttonText: root.uiText
                palette.text: root.uiText
                background: Rectangle {
                    implicitHeight: 44
                    radius: 10
                    color: parent.down ? root.uiPressed : root.uiRaised
                    border.color: parent.activeFocus ? root.uiAccent : root.uiBorder
                }
                text: qsTr("Map")
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
                }
                text: qsTr("Settings")
                Layout.fillWidth: true
                onClicked: root.openPage("settings")
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.uiBorder }
            Label {
                text: qsTr("Tools")
                color: root.uiMuted
                font.pixelSize: 12
                font.bold: true
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                // Layout item implicit sizes differ by translated label length.
                // Force four identically sized cells rather than two uneven columns.
                uniformCellWidths: true
                uniformCellHeights: true
                columnSpacing: 9
                rowSpacing: 9
                ToolMenuButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    text: qsTr("Compass")
                    iconKind: "compass"
                    foregroundColor: root.uiText
                    surfaceColor: root.uiRaised
                    pressedColor: root.uiPressed
                    borderColor: root.uiBorder
                    focusColor: root.uiAccent
                    onClicked: root.openPage("compass")
                }
                ToolMenuButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    text: qsTr("Gradienter")
                    iconKind: "gradienter"
                    foregroundColor: root.uiText
                    surfaceColor: root.uiRaised
                    pressedColor: root.uiPressed
                    borderColor: root.uiBorder
                    focusColor: root.uiAccent
                    onClicked: root.openPage("gradienter")
                }
                ToolMenuButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    text: qsTr("Plumb Bob")
                    iconKind: "plumbbob"
                    foregroundColor: root.uiText
                    surfaceColor: root.uiRaised
                    pressedColor: root.uiPressed
                    borderColor: root.uiBorder
                    focusColor: root.uiAccent
                    onClicked: root.openPage("plumbbob")
                }
                ToolMenuButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    text: flashlightService.enabled ? qsTr("Flashlight On") : qsTr("Flashlight")
                    iconKind: "flashlight"
                    lit: flashlightService.enabled
                    enabled: !flashlightService.busy
                    foregroundColor: root.uiText
                    surfaceColor: root.uiRaised
                    pressedColor: root.uiPressed
                    borderColor: root.uiBorder
                    focusColor: root.uiAccent
                    onClicked: flashlightService.toggle()
                }
            }
            Label {
                visible: flashlightService.error.length > 0
                text: flashlightService.error
                Layout.fillWidth: true
                color: root.uiError
                wrapMode: Text.WordWrap
                font.pixelSize: 12
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.uiBorder }
            Button {
                palette.buttonText: root.uiText
                palette.text: root.uiText
                background: Rectangle {
                    implicitHeight: 44
                    radius: 10
                    color: parent.down ? root.uiPressed : root.uiRaised
                    border.color: parent.activeFocus ? root.uiAccent : root.uiBorder
                }
                text: qsTr("Diagnostics")
                Layout.fillWidth: true
                onClicked: root.openPage("diagnostics")
            }
            Item { Layout.fillHeight: true }
            Label {
                text: qsTr("AOWIS Mobile")
                Layout.fillWidth: true
                color: root.uiMuted
                font.pixelSize: 12
            }
        }
    }

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
        Accessible.name: qsTr("Open navigation menu")
        background: Rectangle {
            radius: 12
            color: root.uiRaised
            border.color: root.uiBorder
        }
        onClicked: navigationDrawer.open()
    }
}
