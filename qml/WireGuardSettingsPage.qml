import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Settings → WireGuard VPN: profiles of the embedded tunnel engine, their
// live status and Start/Stop. Tunnels run inside AOWIS only.
Rectangle {
    id: page
    signal backRequested()

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

    property string selectedId: ""
    readonly property var profileList: wireguardVpnSettings.profiles
    readonly property var selectedStatus: selectedId !== ""
        ? wireguardVpnSettings.status(selectedId, wireguardVpnSettings.statusRevision) : ({})

    color: surfaceColor
    LayoutMirroring.enabled: rightToLeft
    LayoutMirroring.childrenInherit: true

    function stateColor(state) {
        if (state === "connected")
            return page.successColor
        if (state === "error" || state === "stale")
            return page.errorColor
        return page.mutedColor
    }
    function selectedName() {
        return selectedId !== "" ? wireguardVpnSettings.profileName(selectedId) : ""
    }
    onProfileListChanged: {
        if (selectedId !== "" && wireguardVpnSettings.profileName(selectedId) === "")
            selectedId = ""
    }

    component PageButton: Button {
        id: control
        Layout.fillWidth: true
        palette.buttonText: page.textColor
        palette.text: page.textColor
        background: Rectangle {
            implicitHeight: 44
            radius: 10
            color: control.down ? page.pressedColor : page.raisedColor
            border.color: control.activeFocus ? page.accentColor : page.borderColor
            opacity: control.enabled ? 1.0 : 0.45
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 76
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.bottomMargin: 14
        spacing: 10

        PageButton {
            Layout.fillWidth: false
            text: qsTr("Back to Settings")
            onClicked: page.backRequested()
        }
        Label {
            text: qsTr("WireGuard VPN")
            font.pixelSize: 23
            font.bold: true
            color: page.textColor
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Encrypted tunnels that run inside AOWIS only. Other apps and the rest of the phone are not routed through them.")
            color: page.mutedColor
            wrapMode: Text.WordWrap
            font.pixelSize: 12
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: scroll.availableWidth
                spacing: 10

                Label {
                    text: qsTr("Profiles")
                    color: page.accentColor
                    font.bold: true
                    font.pixelSize: 19
                }
                Label {
                    visible: wireguardVpnSettings.storageProblem !== ""
                    Layout.fillWidth: true
                    text: wireguardVpnSettings.storageProblem
                    color: page.errorColor
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                }
                Label {
                    visible: page.profileList.length === 0
                    Layout.fillWidth: true
                    text: qsTr("No profiles yet. Add one by pasting a WireGuard configuration or importing a .conf file.")
                    color: page.mutedColor
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                }
                Repeater {
                    model: page.profileList
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        readonly property var rowStatus: wireguardVpnSettings.status(
                            row.modelData.id, wireguardVpnSettings.statusRevision)
                        readonly property bool selected: page.selectedId === row.modelData.id
                        Layout.fillWidth: true
                        implicitHeight: rowColumn.implicitHeight + 20
                        radius: 10
                        color: selected ? page.selectedColor : (rowArea.pressed ? page.pressedColor : page.raisedColor)
                        border.color: selected ? page.accentColor : page.borderColor
                        Accessible.role: Accessible.Button
                        Accessible.name: row.modelData.name

                        ColumnLayout {
                            id: rowColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 12
                            spacing: 3
                            Label {
                                Layout.fillWidth: true
                                text: row.modelData.name
                                color: row.selected ? page.selectedTextColor : page.textColor
                                font.pixelSize: 16
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: row.modelData.description !== ""
                                text: row.modelData.description
                                color: row.selected ? page.selectedTextColor : page.mutedColor
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: row.modelData.endpoint !== undefined && row.modelData.endpoint !== ""
                                // Host:port is technical text; keep it left-to-right.
                                LayoutMirroring.enabled: false
                                horizontalAlignment: page.rightToLeft ? Text.AlignRight : Text.AlignLeft
                                text: row.modelData.endpoint !== undefined ? row.modelData.endpoint : ""
                                color: row.selected ? page.selectedTextColor : page.mutedColor
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                            }
                            Label {
                                Layout.fillWidth: true
                                text: row.rowStatus.stateText
                                color: row.selected ? page.selectedTextColor : page.stateColor(row.rowStatus.state)
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                        }
                        MouseArea {
                            id: rowArea
                            anchors.fill: parent
                            onClicked: page.selectedId = row.selected ? "" : row.modelData.id
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    PageButton {
                        text: qsTr("Add profile")
                        onClicked: editor.openFor("", "", "", "")
                    }
                    PageButton {
                        text: qsTr("Import .conf file…")
                        onClicked: confFileDialog.open()
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: page.selectedId === "" && wireguardVpnSettings.error !== ""
                    text: wireguardVpnSettings.error
                    color: page.errorColor
                    wrapMode: Text.WordWrap
                }

                // Selected profile: status and actions.
                Rectangle {
                    visible: page.selectedId !== ""
                    Layout.fillWidth: true
                    height: 1
                    color: page.borderColor
                }
                ColumnLayout {
                    visible: page.selectedId !== ""
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: page.selectedName()
                        color: page.accentColor
                        font.bold: true
                        font.pixelSize: 19
                        elide: Text.ElideRight
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 6
                        Label { text: qsTr("State"); color: page.mutedColor; font.pixelSize: 13 }
                        Label {
                            Layout.fillWidth: true
                            text: page.selectedStatus.stateText !== undefined ? page.selectedStatus.stateText : ""
                            color: page.stateColor(page.selectedStatus.state)
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            visible: page.selectedStatus.running === true
                            text: qsTr("Last handshake"); color: page.mutedColor; font.pixelSize: 13
                        }
                        Label {
                            visible: page.selectedStatus.running === true
                            Layout.fillWidth: true
                            text: page.selectedStatus.handshakeText !== undefined ? page.selectedStatus.handshakeText : ""
                            color: page.textColor
                            font.pixelSize: 13
                        }
                        Label {
                            visible: page.selectedStatus.running === true
                            text: qsTr("Traffic"); color: page.mutedColor; font.pixelSize: 13
                        }
                        Label {
                            visible: page.selectedStatus.running === true
                            Layout.fillWidth: true
                            text: page.selectedStatus.trafficText !== undefined ? page.selectedStatus.trafficText : ""
                            color: page.textColor
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            visible: page.selectedStatus.running === true && page.selectedStatus.peerText !== ""
                            text: qsTr("Peer"); color: page.mutedColor; font.pixelSize: 13
                        }
                        Label {
                            visible: page.selectedStatus.running === true && page.selectedStatus.peerText !== ""
                            Layout.fillWidth: true
                            LayoutMirroring.enabled: false
                            horizontalAlignment: page.rightToLeft ? Text.AlignRight : Text.AlignLeft
                            text: page.selectedStatus.peerText !== undefined ? page.selectedStatus.peerText : ""
                            color: page.textColor
                            font.pixelSize: 13
                            elide: Text.ElideMiddle
                        }
                    }
                    PageButton {
                        text: page.selectedStatus.active === true ? qsTr("Stop tunnel") : qsTr("Start tunnel")
                        enabled: wireguardVpnSettings.engineAvailable
                        onClicked: {
                            if (page.selectedStatus.active === true)
                                wireguardVpnSettings.stopTunnel(page.selectedId)
                            else
                                wireguardVpnSettings.startTunnel(page.selectedId)
                        }
                    }
                    Label {
                        readonly property var users: wireguardVpnSettings.profileUsers(
                            page.selectedId, wireguardVpnSettings.statusRevision)
                        Layout.fillWidth: true
                        text: users.length > 0
                              ? qsTr("Used by: %1. While it is used, the tunnel starts automatically and cannot be deleted.").arg(users.join(", "))
                              : qsTr("Not used by any AOWIS connection. Starting the tunnel checks the handshake with the server.")
                        color: page.mutedColor
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        PageButton {
                            text: qsTr("Edit")
                            onClicked: editor.openFor(page.selectedId,
                                                      wireguardVpnSettings.profileName(page.selectedId),
                                                      wireguardVpnSettings.profileDescription(page.selectedId),
                                                      wireguardVpnSettings.profileText(page.selectedId))
                        }
                        PageButton {
                            text: qsTr("Delete")
                            onClicked: deleteDialog.open()
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: wireguardVpnSettings.error !== ""
                        text: wireguardVpnSettings.error
                        color: page.errorColor
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    FileDialog {
        id: confFileDialog
        title: qsTr("Import WireGuard configuration")
        fileMode: FileDialog.OpenFile
        // No name filter: Android's picker filters by MIME type and has none
        // for .conf, which would grey out exactly the files wanted.
        onAccepted: {
            const text = wireguardVpnSettings.readConfFile(selectedFile)
            if (text !== "")
                editor.openFor("", wireguardVpnSettings.suggestedName(selectedFile), "", text)
        }
    }

    // Add, import (review before saving) and edit share one editor.
    Dialog {
        id: editor
        property string profileId: ""
        property string saveError: ""
        readonly property var checked: wireguardVpnSettings.check(confText.text)
        modal: true
        anchors.centerIn: parent
        width: page.width - 24
        height: page.height - 80
        padding: 14
        title: profileId === "" ? qsTr("New WireGuard profile") : qsTr("Edit WireGuard profile")
        palette.windowText: page.textColor
        background: Rectangle { color: page.surfaceColor; radius: 12; border.color: page.borderColor }

        function openFor(id, name, description, text) {
            profileId = id
            saveError = ""
            profileName.text = name
            profileDescription.text = description
            confText.text = text
            open()
        }

        contentItem: ColumnLayout {
            spacing: 8
            Label { text: qsTr("Profile name"); color: page.textColor; font.pixelSize: 14 }
            TextField {
                id: profileName
                Layout.fillWidth: true
                color: page.textColor
                placeholderText: qsTr("e.g. Office")
                placeholderTextColor: page.placeholderColor
                selectionColor: page.selectedColor
                selectedTextColor: page.selectedTextColor
                selectByMouse: true
                onTextEdited: editor.saveError = ""
                background: Rectangle {
                    implicitHeight: 44
                    radius: 9
                    color: page.inputColor
                    border.color: profileName.activeFocus ? page.accentColor : page.borderColor
                }
            }
            TextField {
                id: profileDescription
                Layout.fillWidth: true
                color: page.textColor
                placeholderText: qsTr("Description (optional)")
                placeholderTextColor: page.placeholderColor
                selectionColor: page.selectedColor
                selectedTextColor: page.selectedTextColor
                selectByMouse: true
                onTextEdited: editor.saveError = ""
                background: Rectangle {
                    implicitHeight: 44
                    radius: 9
                    color: page.inputColor
                    border.color: profileDescription.activeFocus ? page.accentColor : page.borderColor
                }
            }
            Label {
                text: qsTr("Configuration (wg-quick .conf)")
                color: page.textColor
                font.pixelSize: 14
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                clip: true
                TextArea {
                    id: confText
                    // Configuration text is always left-to-right.
                    LayoutMirroring.enabled: false
                    horizontalAlignment: TextEdit.AlignLeft
                    color: page.textColor
                    placeholderText: qsTr("Paste [Interface] and [Peer] sections here")
                    placeholderTextColor: page.placeholderColor
                    selectionColor: page.selectedColor
                    selectedTextColor: page.selectedTextColor
                    font.family: "monospace"
                    font.pixelSize: 12
                    wrapMode: TextEdit.WrapAnywhere
                    selectByMouse: true
                    inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhSensitiveData
                    onTextChanged: editor.saveError = ""
                    background: Rectangle {
                        radius: 9
                        color: page.inputColor
                        border.color: confText.activeFocus ? page.accentColor : page.borderColor
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                visible: confText.text.length > 0 && editor.checked.ok && editor.checked.messages.length === 0
                text: qsTr("Configuration is valid.")
                color: page.successColor
                font.pixelSize: 12
            }
            Repeater {
                model: confText.text.length > 0 ? editor.checked.messages.slice(0, 6) : []
                delegate: Label {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.text
                    color: modelData.error ? page.errorColor : page.mutedColor
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
            Label {
                Layout.fillWidth: true
                visible: confText.text.length > 0 && editor.checked.messages.length > 6
                text: qsTr("%1 more messages").arg(editor.checked.messages.length - 6)
                color: page.mutedColor
                font.pixelSize: 12
            }
            Label {
                Layout.fillWidth: true
                visible: editor.saveError !== ""
                text: editor.saveError
                color: page.errorColor
                wrapMode: Text.WordWrap
                font.pixelSize: 12
            }
            Label {
                Layout.fillWidth: true
                visible: editor.profileId !== "" && page.selectedStatus.running === true
                text: qsTr("Saving changes to the configuration stops the running tunnel.")
                color: page.mutedColor
                wrapMode: Text.WordWrap
                font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                PageButton {
                    text: qsTr("Cancel")
                    onClicked: editor.close()
                }
                PageButton {
                    text: qsTr("Save")
                    enabled: profileName.text.trim().length > 0 && editor.checked.ok
                    onClicked: {
                        if (editor.profileId === "") {
                            const id = wireguardVpnSettings.addProfile(profileName.text, profileDescription.text, confText.text)
                            if (id === "") {
                                editor.saveError = wireguardVpnSettings.error
                                return
                            }
                            page.selectedId = id
                        } else if (!wireguardVpnSettings.updateProfile(editor.profileId, profileName.text,
                                                                     profileDescription.text, confText.text)) {
                            editor.saveError = wireguardVpnSettings.error
                            return
                        }
                        editor.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: deleteDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(page.width - 35, 380)
        title: qsTr("Delete WireGuard profile")
        standardButtons: Dialog.Yes | Dialog.No
        palette.windowText: page.textColor
        palette.buttonText: page.textColor
        background: Rectangle { color: page.surfaceColor; radius: 12; border.color: page.borderColor }
        contentItem: Label {
            text: qsTr("Delete the WireGuard profile '%1'? Its keys are removed from this device.").arg(page.selectedName())
            wrapMode: Text.WordWrap
            color: page.textColor
        }
        onAccepted: {
            if (wireguardVpnSettings.removeProfile(page.selectedId))
                page.selectedId = ""
        }
    }
}
