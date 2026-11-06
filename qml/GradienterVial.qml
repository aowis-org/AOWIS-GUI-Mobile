import QtQuick

// Declarative two-axis bullseye level. Only positions/colors change when the
// C++ gravity estimator updates; no Canvas scripting or JS motion filters.
Item {
    id: vial
    property bool darkTheme: true
    property bool hasReading: false
    property bool level: false
    property real rollDegrees: 0
    property real pitchDegrees: 0
    readonly property real diameter: Math.min(width, height)
    readonly property real travel: diameter * 0.265
    readonly property real rollTravel: Math.max(-1, Math.min(1, rollDegrees / 12.0)) * travel
    readonly property real pitchTravel: Math.max(-1, Math.min(1, pitchDegrees / 12.0)) * travel
    readonly property color faceColor: darkTheme ? "#0c2736" : "#e2eef2"
    readonly property color ringColor: darkTheme ? "#77c5b5" : "#347b72"
    Accessible.name: hasReading
                     ? "Spirit level, roll " + rollDegrees.toFixed(1) +
                       " degrees, pitch " + pitchDegrees.toFixed(1) + " degrees"
                     : "Spirit level, waiting for gravity sensor"

    Rectangle {
        anchors.centerIn: parent
        width: vial.diameter
        height: width
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0; color: vial.darkTheme ? "#b9d5d8" : "#748b94" }
            GradientStop { position: 0.24; color: vial.darkTheme ? "#607f89" : "#d4e0e4" }
            GradientStop { position: 0.64; color: vial.darkTheme ? "#284958" : "#7e9ca9" }
            GradientStop { position: 1; color: vial.darkTheme ? "#a7cad1" : "#e7f0f2" }
        }
        border.width: 2
        border.color: vial.darkTheme ? "#c7e9e4" : "#6b838d"
    }

    Rectangle {
        id: face
        anchors.centerIn: parent
        width: vial.diameter * .89
        height: width
        radius: width / 2
        color: vial.faceColor
        border.width: Math.max(2, vial.diameter * .013)
        border.color: vial.darkTheme ? "#09212b" : "#c6d8de"

        // Machined concentric vial grooves; the center ring represents the
        // precision tolerance zone rather than an arbitrary colored icon.
        Repeater {
            model: [0.73, 0.48, 0.25]
            Rectangle {
                required property real modelData
                anchors.centerIn: parent
                width: face.width * modelData
                height: width
                radius: width / 2
                color: "transparent"
                border.width: modelData < .3 ? 2.5 : 1.5
                border.color: vial.ringColor
                opacity: modelData < .3 ? .9 : .4
            }
        }

        // Reticle guides, protected from font differences on Android.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * .85
            height: 1
            color: vial.ringColor
            opacity: .45
        }
        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: parent.height * .85
            color: vial.ringColor
            opacity: .45
        }
        Repeater {
            model: 24
            Item {
                anchors.fill: face
                rotation: index * 15
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: face.width * .045
                    width: index % 3 === 0 ? 3 : 1.5
                    height: index % 3 === 0 ? face.height * .045 : face.height * .026
                    radius: 1
                    color: vial.ringColor
                    opacity: index % 3 === 0 ? .9 : .45
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: face.width * .035
            height: width
            radius: width / 2
            color: vial.ringColor
            opacity: .65
        }
    }

    // A shadow below the glass bubble gives the level a physical depth.
    Rectangle {
        width: vial.diameter * .168
        height: width
        radius: width / 2
        color: "#071e29"
        opacity: vial.hasReading ? .42 : 0
        x: (vial.width - width) / 2 + vial.rollTravel + 3
        y: (vial.height - height) / 2 - vial.pitchTravel + 5
    }
    Rectangle {
        id: bubble
        width: vial.diameter * .165
        height: width
        radius: width / 2
        x: (vial.width - width) / 2 + vial.rollTravel
        y: (vial.height - height) / 2 - vial.pitchTravel
        opacity: vial.hasReading ? 1 : .3
        gradient: Gradient {
            GradientStop { position: 0.0; color: vial.level ? "#f0fff4" : "#f9ffe6" }
            GradientStop { position: 0.48; color: vial.level ? "#78f4b9" : "#d4e988" }
            GradientStop { position: 1; color: vial.level ? "#229466" : "#8b9c4b" }
        }
        border.width: 2
        border.color: vial.level ? "#d2ffe8" : "#f5ffd0"
        Rectangle {
            x: parent.width * .19
            y: parent.height * .14
            width: parent.width * .32
            height: parent.height * .16
            radius: height / 2
            rotation: -25
            color: "#ffffff"
            opacity: .58
        }
    }
}
