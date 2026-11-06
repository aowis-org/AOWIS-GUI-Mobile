import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Touch-first equivalent of Desktop Settings > Units. The profile choices
// and presented quantity registry are supplied by the shared MODEL metadata.
Rectangle {
    id: page
    signal backRequested()

    property color surfaceColor: "#102535"
    property color raisedColor: "#254354"
    property color inputColor: "#19384b"
    property color textColor: "#f2f8fa"
    property color mutedColor: "#b5ccd5"
    property color accentColor: "#a3ead8"
    property color borderColor: "#6c91a1"
    property color selectedColor: "#346f86"
    property color selectedTextColor: "#ffffff"
    property bool rightToLeft: false
    property string noticeKey: ""
    color: surfaceColor
    LayoutMirroring.enabled: rightToLeft
    LayoutMirroring.childrenInherit: true

    function startEdit(renaming) {
        profileDialog.renaming = renaming
        profileName.text = renaming ? unitProfileSettings.activeName
                                    : qsTr("%1 (custom)").arg(unitProfileSettings.activeName)
        profileDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 76
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.bottomMargin: 14
        spacing: 10
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Button {
                text: qsTr("Back to Settings")
                onClicked: page.backRequested()
                palette.buttonText: page.textColor
                background: Rectangle {
                    implicitHeight: 42
                    radius: 9
                    color: page.raisedColor
                    border.color: page.borderColor
                }
            }
            Label {
                text: qsTr("Unit Profiles")
                font.pixelSize: 23
                font.bold: true
                color: page.textColor
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Display-unit preferences only. Canonical model values remain unchanged.")
            color: page.mutedColor
            wrapMode: Text.WordWrap
            font.pixelSize: 12
        }
        ComboBox {
            id: detailedProfileSelector
            Layout.fillWidth: true
            model: unitProfileSettings.listProfiles(languageSettings.language, unitProfileSettings.revision)
            textRole: "label"
            currentIndex: unitProfileSettings.currentIndex
            onActivated: function(index) { unitProfileSettings.selectProfile(index) }
            palette.text: page.textColor
            palette.buttonText: page.textColor
            palette.base: page.inputColor
            palette.highlight: page.selectedColor
            palette.highlightedText: page.selectedTextColor
            indicator: UiIcon {
                kind: "down"
                strokeColor: page.textColor
                width: 22
                height: 22
                x: page.rightToLeft ? 12 : parent.width - width - 12
                y: (parent.height - height) / 2
            }
            contentItem: Text {
                text: detailedProfileSelector.displayText
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
                border.color: page.borderColor
            }
            delegate: ItemDelegate {
                width: detailedProfileSelector.width
                text: modelData.label
                highlighted: detailedProfileSelector.highlightedIndex === index
                palette.text: page.textColor
                palette.highlightedText: page.selectedTextColor
                background: Rectangle { color: parent.highlighted ? page.selectedColor : page.inputColor }
            }
            popup: Popup {
                y: detailedProfileSelector.height - 1
                width: detailedProfileSelector.width
                implicitHeight: Math.min(contentItem.implicitHeight + 4, 320)
                padding: 2
                contentItem: ListView {
                    implicitHeight: contentHeight
                    clip: true
                    model: detailedProfileSelector.popup.visible ? detailedProfileSelector.delegateModel : null
                    currentIndex: detailedProfileSelector.highlightedIndex
                    ScrollIndicator.vertical: ScrollIndicator { }
                }
                background: Rectangle { radius: 8; color: page.inputColor; border.color: page.borderColor }
            }
        }
        Button {
            Layout.fillWidth: true
            text: qsTr("New custom profile")
            onClicked: page.startEdit(false)
            palette.buttonText: page.textColor
            background: Rectangle { implicitHeight: 44; radius: 9; color: page.raisedColor; border.color: page.borderColor }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Button {
                Layout.fillWidth: true
                text: qsTr("Rename")
                enabled: unitProfileSettings.editable
                onClicked: page.startEdit(true)
                palette.buttonText: page.textColor
                background: Rectangle { implicitHeight: 44; radius: 9; color: page.raisedColor; border.color: page.borderColor; opacity: parent.enabled ? 1 : 0.4 }
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Delete")
                enabled: unitProfileSettings.editable
                onClicked: deleteDialog.open()
                palette.buttonText: page.textColor
                background: Rectangle { implicitHeight: 44; radius: 9; color: page.raisedColor; border.color: page.borderColor; opacity: parent.enabled ? 1 : 0.4 }
            }
        }
        Label {
            visible: page.noticeKey.length > 0
            text: page.noticeKey === "unit" ? qsTr("Could not update the unit profile.")
                                            : qsTr("Profile name is empty or already in use.")
            color: page.accentColor
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        TextField {
            id: unitSearch
            Layout.fillWidth: true
            placeholderText: qsTr("Search quantities or units…")
            color: page.textColor
            placeholderTextColor: page.mutedColor
            selectionColor: page.selectedColor
            selectedTextColor: page.selectedTextColor
            background: Rectangle {
                implicitHeight: 44
                radius: 9
                color: page.inputColor
                border.color: unitSearch.activeFocus ? page.accentColor : page.borderColor
            }
        }
        ListView {
            id: quantities
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: unitProfileSettings.fields(unitSearch.text, unitProfileSettings.revision, languageSettings.language)
            ScrollBar.vertical: ScrollBar { }
            delegate: Column {
                id: fieldRow
                required property var modelData
                width: quantities.width - 10
                spacing: 6
                Item { width: 1; height: fieldRow.modelData.groupStart ? 13 : 5 }
                Label {
                    visible: fieldRow.modelData.groupStart
                    text: fieldRow.modelData.group
                    font.bold: true
                    color: page.accentColor
                    font.pixelSize: 16
                    width: fieldRow.width
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: fieldRow.modelData.label
                    font.pixelSize: 14
                    color: page.textColor
                    width: fieldRow.width
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    width: fieldRow.width
                    spacing: 8
                    ComboBox {
                        id: quantityUnit
                        Layout.fillWidth: true
                        model: fieldRow.modelData.choices
                        textRole: "label"
                        currentIndex: fieldRow.modelData.selected
                        enabled: unitProfileSettings.editable && fieldRow.modelData.choices.length > 1
                        onActivated: function(index) {
                            if (!unitProfileSettings.setUnit(fieldRow.modelData.key, fieldRow.modelData.choices[index].unit))
                                page.noticeKey = "unit"
                            else
                                page.noticeKey = ""
                        }
                        palette.text: page.textColor
                        palette.buttonText: page.textColor
                        palette.base: page.inputColor
                        palette.highlight: page.selectedColor
                        palette.highlightedText: page.selectedTextColor
                        indicator: UiIcon {
                            kind: "down"
                            strokeColor: page.textColor
                            width: 18; height: 18
                            x: page.rightToLeft ? 12 : parent.width - width - 12
                            y: (parent.height - height) / 2
                        }
                        contentItem: Text {
                            text: quantityUnit.displayText
                            color: page.textColor
                            leftPadding: page.rightToLeft ? 32 : 12
                            rightPadding: page.rightToLeft ? 12 : 32
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        delegate: ItemDelegate {
                            width: quantityUnit.width
                            text: modelData.label
                            highlighted: quantityUnit.highlightedIndex === index
                            palette.text: page.textColor
                            palette.highlightedText: page.selectedTextColor
                            background: Rectangle { color: parent.highlighted ? page.selectedColor : page.inputColor }
                        }
                        background: Rectangle {
                            implicitHeight: 42
                            radius: 8
                            color: page.inputColor
                            border.color: page.borderColor
                            opacity: parent.enabled ? 1 : 0.6
                        }
                        popup: Popup {
                            y: quantityUnit.height - 1
                            width: quantityUnit.width
                            implicitHeight: Math.min(contentItem.implicitHeight + 4, 260)
                            padding: 2
                            contentItem: ListView {
                                implicitHeight: contentHeight
                                clip: true
                                model: quantityUnit.popup.visible ? quantityUnit.delegateModel : null
                                currentIndex: quantityUnit.highlightedIndex
                                ScrollIndicator.vertical: ScrollIndicator { }
                            }
                            background: Rectangle { radius: 8; color: page.inputColor; border.color: page.borderColor }
                        }
                    }
                    Label {
                        text: qsTr("Canonical: %1").arg(fieldRow.modelData.canonical)
                        color: page.mutedColor
                        font.pixelSize: 11
                    }
                }
                Rectangle { width: fieldRow.width; height: 1; color: page.borderColor; opacity: 0.4 }
            }
        }
    }

    Dialog {
        id: profileDialog
        property bool renaming: false
        modal: true
        anchors.centerIn: parent
        width: Math.min(page.width - 35, 380)
        title: renaming ? qsTr("Rename unit profile") : qsTr("New custom profile")
        standardButtons: Dialog.Ok | Dialog.Cancel
        palette.windowText: page.textColor
        palette.buttonText: page.textColor
        background: Rectangle { color: page.surfaceColor; radius: 12; border.color: page.borderColor }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: qsTr("Profile name"); color: page.textColor }
            TextField {
                id: profileName
                Layout.fillWidth: true
                color: page.textColor
                selectByMouse: true
                background: Rectangle { implicitHeight: 45; color: page.inputColor; radius: 9; border.color: page.borderColor }
            }
        }
        onAccepted: {
            const succeeded = renaming ? unitProfileSettings.renameProfile(profileName.text)
                                       : unitProfileSettings.createProfile(profileName.text)
            page.noticeKey = succeeded ? "" : "profile"
        }
    }
    Dialog {
        id: deleteDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(page.width - 35, 380)
        title: qsTr("Delete unit profile")
        standardButtons: Dialog.Yes | Dialog.No
        palette.windowText: page.textColor
        palette.buttonText: page.textColor
        background: Rectangle { color: page.surfaceColor; radius: 12; border.color: page.borderColor }
        contentItem: Label {
            text: qsTr("Delete the custom unit profile '%1'?").arg(unitProfileSettings.activeName)
            wrapMode: Text.WordWrap
            color: page.textColor
        }
        onAccepted: unitProfileSettings.removeProfile()
    }
}
