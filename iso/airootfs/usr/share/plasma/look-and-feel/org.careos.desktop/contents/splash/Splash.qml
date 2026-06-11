import QtQuick 2.15

Rectangle {
    id: root
    width: Screen.width || 1920
    height: Screen.height || 1080
    color: "#080d17"

    property int stage: 0

    Rectangle {
        x: root.width * 0.16
        y: root.height * 0.18
        width: root.width * 0.68
        height: 2
        color: "#2c3c58"
        opacity: 0.9
    }

    Rectangle {
        x: root.width * 0.16
        y: root.height * 0.18
        width: root.width * (0.14 + (stage % 5) * 0.08)
        height: 2
        color: "#559aff"
        Behavior on width { NumberAnimation { duration: 180 } }
    }

    Column {
        anchors.centerIn: parent
        spacing: root.height * 0.022

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "CareOS"
            font.pixelSize: root.height * 0.075
            font.weight: Font.Light
            color: "#eaf0ff"
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "CareOS Shell"
            font.pixelSize: root.height * 0.018
            font.bold: true
            font.letterSpacing: 2
            color: "#82bcff"
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.width * 0.006
            Repeater {
                model: 5
                Rectangle {
                    width: root.width * 0.028
                    height: 3
                    radius: 2
                    color: index < (stage % 6) ? "#2ecc8e" : "#2c3c58"
                    Behavior on color { ColorAnimation { duration: 160 } }
                }
            }
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.height * 0.77
        text: "Plasma desktop on an Arch Linux core"
        color: "#8a99ba"
        font.pixelSize: root.height * 0.016
    }

    Timer {
        interval: 220
        running: true
        repeat: true
        onTriggered: stage = (stage + 1) % 6
    }
}
