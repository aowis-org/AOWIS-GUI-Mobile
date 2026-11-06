import QtQuick

// Pure QML geometry. All gravity sampling, filtering and timing live in C++.
Item {
    id: instrument
    property bool darkTheme: true
    property bool hasReading: false
    property bool plumb: false
    property real downX: 0
    property real downY: 1
    readonly property real size: Math.min(width, height)
    readonly property real ropeLength: size * 0.285
    readonly property color metal: darkTheme ? "#b9d6d9" : "#879daa"
    readonly property color engraving: darkTheme ? "#8eb8bd" : "#47707e"
    readonly property color glow: plumb ? "#72f1ba" : "#f3bf68"
    Accessible.name: !hasReading ? "Plumb bob, waiting for upright gravity reading"
                     : plumb ? "Plumb bob, phone aligned vertically"
                     : "Plumb bob, phone not vertically aligned"

    Rectangle {
        anchors.centerIn: parent
        width: instrument.size
        height: width
        radius: width * 0.11
        gradient: Gradient {
            GradientStop { position: 0; color: instrument.darkTheme ? "#728f9b" : "#839da8" }
            GradientStop { position: .25; color: instrument.darkTheme ? "#2d5060" : "#d4e4eb" }
            GradientStop { position: .8; color: instrument.darkTheme ? "#193542" : "#aac6d1" }
            GradientStop { position: 1; color: instrument.darkTheme ? "#9ac3c8" : "#f4fbfc" }
        }
        border.color: instrument.metal
        border.width: Math.max(2, instrument.size * .009)
    }
    Rectangle {
        id: chamber
        anchors.centerIn: parent
        width: instrument.size * .94
        height: width
        radius: width * .1
        gradient: Gradient {
            GradientStop { position: 0; color: instrument.darkTheme ? "#112e3e" : "#e0edf2" }
            GradientStop { position: 0.5; color: instrument.darkTheme ? "#071b27" : "#f7fbfc" }
            GradientStop { position: 1; color: instrument.darkTheme ? "#153444" : "#d2e5ed" }
        }
        border.color: instrument.darkTheme ? "#526e78" : "#9cb9c3"
        border.width: 2
        clip: true

        // Fine machined guide marks, and fixed crosshair for true vertical.
        Repeater {
            model: 25
            Rectangle {
                required property int index
                width: index % 4 === 0 ? chamber.width * .115 : chamber.width * .060
                height: index % 4 === 0 ? 2 : 1
                x: chamber.width * .055
                y: chamber.height * (.085 + index * .034)
                color: instrument.engraving
                opacity: index % 4 === 0 ? .85 : .4
            }
        }
        Repeater {
            model: 25
            Rectangle {
                required property int index
                width: index % 4 === 0 ? chamber.width * .115 : chamber.width * .060
                height: index % 4 === 0 ? 2 : 1
                x: chamber.width - width - chamber.width * .055
                y: chamber.height * (.085 + index * .034)
                color: instrument.engraving
                opacity: index % 4 === 0 ? .85 : .4
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: 2
            height: parent.height * .82
            color: instrument.engraving
            opacity: .26
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * .76
            height: 1
            color: instrument.engraving
            opacity: .23
        }
        Rectangle {
            anchors.centerIn: parent
            width: chamber.width * .18
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 1.5
            border.color: instrument.glow
            opacity: .6
        }
        Rectangle {
            anchors.centerIn: parent
            width: chamber.width * .38
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: instrument.engraving
            opacity: .32
        }

        // The pendulum is a continuous geometric transform of the filtered
        // C++ gravity direction. There is NO QML tween, timer or JS filter.
        Item {
            id: pendulum
            x: (chamber.width - width) / 2
            y: chamber.height / 2
            width: 24
            height: instrument.ropeLength
            opacity: instrument.hasReading ? 1 : .22
            transform: Rotation {
                origin.x: pendulum.width / 2
                origin.y: 0
                angle: -Math.atan2(instrument.downX, instrument.downY) * 180 / Math.PI
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 3
                height: parent.height
                gradient: Gradient {
                    GradientStop { position: 0; color: "#fff3be" }
                    GradientStop { position: .55; color: "#d4ac65" }
                    GradientStop { position: 1; color: "#8a6734" }
                }
                border.color: "#7b663e"
                border.width: 1
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: parent.height - instrument.size * .028
                width: instrument.size * .040
                height: instrument.size * .050
                radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: "#fff1b3" }
                    GradientStop { position: 1; color: "#97733c" }
                }
                border.color: "#755b37"
            }
            Rectangle {
                id: weight
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height + instrument.size * .007
                width: instrument.size * .125
                height: instrument.size * .13
                radius: instrument.size * .022
                gradient: Gradient {
                    GradientStop { position: 0; color: "#fff8ce" }
                    GradientStop { position: .26; color: "#ddb977" }
                    GradientStop { position: .7; color: "#9c6931" }
                    GradientStop { position: 1; color: "#f2d99c" }
                }
                border.color: "#b18743"
                border.width: 1.5
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * .67
                    height: parent.height * .3
                    radius: height * .4
                    color: "#765223"
                    opacity: .32
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.bottom
                    anchors.topMargin: -instrument.size * .018
                    width: instrument.size * .033
                    height: width
                    rotation: 45
                    color: "#d4aa65"
                    border.color: "#86612f"
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: instrument.size * .055
            height: width
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#f8f6d2" }
                GradientStop { position: 1; color: "#9c7c42" }
            }
            border.color: instrument.glow
            border.width: 2
            opacity: instrument.hasReading ? 1 : .4
        }
        Text {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: chamber.height * .06
            text: qsTr("PLUMB  /  GRAVITY")
            font.pixelSize: Math.max(9, instrument.size * .026)
            font.bold: true
            font.letterSpacing: 2
            color: instrument.engraving
        }
    }
}
