import QtQuick
import QtQuick.Controls.Basic

// Shared tool tile for the navigation drawer. Drawn icons, never font glyphs.
Button {
    id: tile
    property string iconKind: "compass"
    property color foregroundColor: "#f2f8fa"
    property color surfaceColor: "#254354"
    property color pressedColor: "#35586a"
    property color borderColor: "#6c91a1"
    property color focusColor: "#a3ead8"
    property bool lit: false

    implicitHeight: 92
    padding: 8
    contentItem: Column {
        spacing: 5
        UiIcon {
            kind: tile.iconKind
            strokeColor: tile.foregroundColor
            width: 31
            height: 31
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            width: parent.width
            text: tile.text
            color: tile.foregroundColor
            font.pixelSize: 13
            font.bold: tile.lit
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 2
        }
    }
    background: Rectangle {
        implicitHeight: 92
        radius: 12
        color: tile.down ? tile.pressedColor : tile.surfaceColor
        border.width: tile.lit ? 2 : 1
        border.color: tile.lit || tile.activeFocus ? tile.focusColor : tile.borderColor
    }
}
