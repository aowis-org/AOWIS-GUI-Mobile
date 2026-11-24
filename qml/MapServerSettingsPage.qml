import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Settings → Network & servers → Map server: the aowis-server-map endpoint
// and the connection it is reached through (direct or a WireGuard profile).
Rectangle {
    id: page
    signal backRequested()
    signal wireguardRequested()

    property color surfaceColor: "#102535"
    property color raisedColor: "#254354"
    property color pressedColor: "#35586a"
    property color inputColor: "#19384b"
    property color textColor: "#f2f8fa"
    property color mutedColor: "#b5ccd5"
    property color placeholderColor: "#9fb8c4"
    property color accentColor: "#a3ead8"
    property color borderColor: "#6c91a1"
    property color selectedColor: "#346f86"
    property color selectedTextColor: "#ffffff"
    property color successColor: "#a2ebc2"
    property color errorColor: "#ffb7ad"
    property bool rightToLeft: false

    property string saveFeedback: ""
    property bool saveSucceeded: false

    color: surfaceColor
    LayoutMirroring.enabled: rightToLeft
    LayoutMirroring.childrenInherit: true

    // The saved values, each time the page opens (unsaved edits are dropped).
    function reload() {
        mapServerUrl.text = mapServerSettings.serverUrl
        mapServerPort.text = mapServerSettings.serverPort > 0 ? String(mapServerSettings.serverPort) : ""
        page.saveFeedback = ""
        mapServerTunnel.selectId(mapServerSettings.wireguardProfileId)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 76
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.bottomMargin: 14
        spacing: 10

        Button {
            id: backButton
            text: qsTr("Back to Settings")
            onClicked: page.backRequested()
            palette.buttonText: page.textColor
            background: Rectangle {
                implicitHeight: 42
                radius: 9
                color: backButton.down ? page.pressedColor : page.raisedColor
                border.color: page.borderColor
            }
        }
        Label {
            text: qsTr("Map server")
            font.pixelSize: 23
            font.bold: true
            color: page.textColor
            Layout.fillWidth: true
            elide: Text.ElideRight
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: scroll.availableWidth
                spacing: 12
                Label {
                    text: qsTr("Server URL")
                    color: page.textColor
                    font.pixelSize: 14
                }
                TextField {
                    id: mapServerUrl
                    LayoutMirroring.enabled: false
                    horizontalAlignment: TextInput.AlignLeft
                    color: page.textColor
                    placeholderTextColor: page.placeholderColor
                    selectionColor: page.selectedColor
                    selectedTextColor: page.selectedTextColor
                    background: Rectangle {
                        implicitHeight: 48
                        radius: 10
                        color: page.inputColor
                        border.color: mapServerUrl.activeFocus ? page.accentColor : page.borderColor
                    }
                    Layout.fillWidth: true
                    placeholderText: qsTr("http://192.168.2.10")
                    selectByMouse: true
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                    onTextEdited: page.saveFeedback = ""
                }
                Label {
                    text: qsTr("Use http:// or https://. Enter the port separately. The default AOWIS MAP server port is 8122.")
                    color: page.mutedColor
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Port")
                    color: page.textColor
                    font.pixelSize: 14
                }
                TextField {
                    id: mapServerPort
                    LayoutMirroring.enabled: false
                    horizontalAlignment: TextInput.AlignLeft
                    color: page.textColor
                    placeholderTextColor: page.placeholderColor
                    selectionColor: page.selectedColor
                    selectedTextColor: page.selectedTextColor
                    background: Rectangle {
                        implicitHeight: 48
                        radius: 10
                        color: page.inputColor
                        border.color: mapServerPort.activeFocus ? page.accentColor : page.borderColor
                    }
                    Layout.fillWidth: true
                    placeholderText: qsTr("e.g. 8122")
                    selectByMouse: true
                    maximumLength: 5
                    inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhNoPredictiveText
                    validator: IntValidator { bottom: 1; top: 65535 }
                    onTextEdited: page.saveFeedback = ""
                }
                Label {
                    visible: wireguardVpnSettings.engineAvailable
                    text: qsTr("Connection")
                    color: page.textColor
                    font.pixelSize: 14
                }
                ComboBox {
                    id: mapServerTunnel
                    visible: wireguardVpnSettings.engineAvailable
                    Layout.fillWidth: true
                    textRole: "label"
                    // Direct first, then the WireGuard profiles; a saved profile
                    // that no longer exists stays visible (and blocks the map).
                    property string pendingId: ""
                    readonly property var choices: {
                        const list = [{ id: "", label: qsTr("Direct (no VPN)") }]
                        const profiles = wireguardVpnSettings.profiles
                        let found = pendingId === ""
                        for (let i = 0; i < profiles.length; ++i) {
                            list.push({ id: profiles[i].id, label: qsTr("WireGuard: %1").arg(profiles[i].name) })
                            found = found || profiles[i].id === pendingId
                        }
                        if (!found)
                            list.push({ id: pendingId, label: qsTr("Missing WireGuard profile") })
                        return list
                    }
                    model: choices
                    readonly property string selectedId: currentIndex >= 0 && currentIndex < choices.length
                                                        ? choices[currentIndex].id : ""
                    function selectId(id) {
                        pendingId = id
                        for (let i = 0; i < choices.length; ++i) {
                            if (choices[i].id === id) {
                                currentIndex = i
                                return
                            }
                        }
                        currentIndex = 0
                    }
                    onChoicesChanged: selectId(pendingId)
                    onActivated: function(index) {
                        pendingId = choices[index].id
                        page.saveFeedback = ""
                    }
                    palette.text: page.textColor
                    palette.buttonText: page.textColor
                    palette.base: page.inputColor
                    palette.window: page.inputColor
                    palette.highlight: page.selectedColor
                    palette.highlightedText: page.selectedTextColor
                    indicator: UiIcon {
                        kind: "down"
                        strokeColor: page.textColor
                        width: 22
                        height: 22
                        x: page.rightToLeft ? 14 : mapServerTunnel.width - width - 14
                        y: (mapServerTunnel.height - height) / 2
                    }
                    contentItem: Text {
                        text: mapServerTunnel.displayText
                        color: page.textColor
                        leftPadding: page.rightToLeft ? 32 : 12
                        rightPadding: page.rightToLeft ? 12 : 32
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        implicitHeight: 48
                        radius: 10
                        color: page.inputColor
                        border.color: mapServerTunnel.activeFocus ? page.accentColor : page.borderColor
                    }
                    delegate: ItemDelegate {
                        width: mapServerTunnel.width
                        text: modelData.label
                        highlighted: mapServerTunnel.highlightedIndex === index
                        palette.text: page.textColor
                        palette.highlightedText: page.selectedTextColor
                        background: Rectangle {
                            color: parent.highlighted ? page.selectedColor : page.inputColor
                        }
                    }
                    popup: Popup {
                        y: mapServerTunnel.height - 1
                        width: mapServerTunnel.width
                        implicitHeight: Math.min(contentItem.implicitHeight + 4, 240)
                        padding: 2
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: mapServerTunnel.popup.visible ? mapServerTunnel.delegateModel : null
                            currentIndex: mapServerTunnel.highlightedIndex
                            ScrollIndicator.vertical: ScrollIndicator { }
                        }
                        background: Rectangle {
                            radius: 10
                            color: page.inputColor
                            border.color: page.borderColor
                        }
                    }
                }
                Label {
                    visible: wireguardVpnSettings.engineAvailable && mapServerTunnel.selectedId !== ""
                    text: qsTr("Tiles, terrain and imagery are then loaded only through this tunnel, never directly.")
                    color: page.mutedColor
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Button {
                    palette.buttonText: page.textColor
                    palette.text: page.textColor
                    background: Rectangle {
                        implicitHeight: 44
                        radius: 10
                        color: parent.down ? page.pressedColor : page.raisedColor
                        border.color: parent.activeFocus ? page.accentColor : page.borderColor
                        opacity: parent.enabled ? 1.0 : 0.55
                    }
                    
                    text: qsTr("Save map server")
                    Layout.fillWidth: true
                    enabled: mapServerUrl.text.trim().length > 0 && mapServerPort.acceptableInput
                    onClicked: {
                        page.saveSucceeded = mapServerSettings.save(
                                    mapServerUrl.text, mapServerPort.text, mapServerTunnel.selectedId)
                        page.saveFeedback = page.saveSucceeded ? "saved" : "error"
                        if (page.saveSucceeded) {
                            wireguardVpnSettings.notifyUsersChanged()
                            mapServerUrl.text = mapServerSettings.serverUrl
                            mapServerPort.text = String(mapServerSettings.serverPort)
                            mapServerUrl.focus = false
                            mapServerPort.focus = false
                        }
                    }
                }
                Label {
                    text: page.saveSucceeded ? qsTr("Map server saved on this device.") : mapServerSettings.error
                    visible: page.saveFeedback !== ""
                    color: page.saveSucceeded ? page.successColor : page.errorColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: !mapServerSettings.configured
                          ? qsTr("No map server configured yet.")
                          : mapServerSettings.wireguardProfileId === ""
                            ? qsTr("Saved endpoint: %1").arg(mapServerSettings.endpointUrl)
                            : qsTr("Saved endpoint: %1 through WireGuard “%2”").arg(mapServerSettings.endpointUrl)
                                  .arg(wireguardVpnSettings.profileName(mapServerSettings.wireguardProfileId) !== ""
                                       ? wireguardVpnSettings.profileName(mapServerSettings.wireguardProfileId)
                                       : qsTr("Missing WireGuard profile"))
                    color: page.textColor
                    font.pixelSize: 13
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Map tiles, terrain and imagery providers are loaded from this aowis-server-map.")
                    color: page.mutedColor
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Button {
                    id: manageTunnels
                    visible: wireguardVpnSettings.engineAvailable
                    Layout.fillWidth: true
                    text: qsTr("Manage WireGuard profiles…")
                    palette.buttonText: page.textColor
                    palette.text: page.textColor
                    background: Rectangle {
                        implicitHeight: 44
                        radius: 10
                        color: manageTunnels.down ? page.pressedColor : page.raisedColor
                        border.color: manageTunnels.activeFocus ? page.accentColor : page.borderColor
                    }
                    onClicked: page.wireguardRequested()
                }
            }
        }
    }
}
