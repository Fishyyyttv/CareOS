import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import SddmComponents 2.0

Item {
    id: root
    width:  Screen.width
    height: Screen.height

    property int sessionIndex: sessionModel.lastIndex

    // ── Background ────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: "#080d17"

        // Subtle center glow
        Rectangle {
            anchors.centerIn: parent
            width:  700
            height: 500
            radius: 350
            color:  "#559aff"
            opacity: 0.045
            layer.enabled: true
            layer.effect: Item {
                // soft edges via large radius; no import needed
            }
        }

        // Very subtle grid
        Canvas {
            anchors.fill: parent
            opacity: 0.035
            onPaint: {
                var ctx = getContext("2d")
                ctx.strokeStyle = "#559aff"
                ctx.lineWidth = 1
                for (var x = 0; x < width; x += 192) {
                    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                }
                for (var y = 0; y < height; y += 108) {
                    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
                }
            }
        }
    }

    // ── Clock — top right ─────────────────────────────────────────────────────
    Column {
        anchors {
            top:    parent.top
            right:  parent.right
            margins: 48
        }
        spacing: 4

        Text {
            id: clockTime
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatTime(new Date(), "hh:mm")
            font { pixelSize: 56; weight: Font.Light; family: "Noto Sans" }
            color: "#eaf0ff"
        }
        Text {
            id: clockDate
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatDate(new Date(), "dddd, MMMM d")
            font { pixelSize: 15; family: "Noto Sans" }
            color: "#8a99ba"
        }

        Timer {
            interval: 1000; running: true; repeat: true
            onTriggered: {
                clockTime.text = Qt.formatTime(new Date(), "hh:mm")
                clockDate.text = Qt.formatDate(new Date(), "dddd, MMMM d")
            }
        }
    }

    // ── Login card — center ───────────────────────────────────────────────────
    Column {
        anchors.centerIn: parent
        spacing: 14

        // Wordmark
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "CareOS"
                font { pixelSize: 44; weight: Font.Light; letterSpacing: 5; family: "Noto Sans" }
                color: "#eaf0ff"
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 64; height: 2; color: "#559aff"
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "CareOS Shell"
                font { pixelSize: 13; weight: Font.Medium; letterSpacing: 2; family: "Noto Sans" }
                color: "#82bcff"
            }
        }

        Item { height: 20 }

        // Username field
        TextField {
            id: usernameField
            width: 320; height: 46
            placeholderText: "Username"
            text: userModel.lastUser
            font { pixelSize: 14; family: "Noto Sans" }
            color: "#eaf0ff"
            leftPadding: 16

            background: Rectangle {
                color: "#0a1220"
                radius: 7
                border.color: usernameField.activeFocus ? "#559aff" : "#2c3c58"
                border.width: 1
                Behavior on border.color { ColorAnimation { duration: 120 } }
            }

            Keys.onReturnPressed: passwordField.forceActiveFocus()
            Keys.onTabPressed:    passwordField.forceActiveFocus()
        }

        // Password field
        TextField {
            id: passwordField
            width: 320; height: 46
            placeholderText: "Password"
            echoMode: TextInput.Password
            font { pixelSize: 14; family: "Noto Sans" }
            color: "#eaf0ff"
            leftPadding: 16

            background: Rectangle {
                color: "#0a1220"
                radius: 7
                border.color: passwordField.activeFocus ? "#559aff" : "#2c3c58"
                border.width: 1
                Behavior on border.color { ColorAnimation { duration: 120 } }
            }

            Keys.onReturnPressed: doLogin()
            Keys.onTabPressed:    usernameField.forceActiveFocus()
        }

        // Sign in button
        Button {
            id: loginButton
            width: 320; height: 46

            background: Rectangle {
                color: loginButton.pressed ? "#2f6fc8"
                     : loginButton.hovered ? "#6aabff"
                     : "#559aff"
                radius: 7
                Behavior on color { ColorAnimation { duration: 120 } }
            }

            contentItem: Text {
                text: "Sign In"
                font { pixelSize: 14; weight: Font.Medium; family: "Noto Sans" }
                color: "#080d17"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment:   Text.AlignVCenter
            }

            onClicked: doLogin()
        }

        // Error message
        Text {
            id: errorMsg
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#f56060"
            font { pixelSize: 13; family: "Noto Sans" }
            visible: text !== ""
        }
    }

    // ── Session selector — bottom left ────────────────────────────────────────
    ComboBox {
        id: sessionCombo
        anchors { bottom: parent.bottom; left: parent.left; margins: 36 }
        width: 190; height: 38
        model: sessionModel
        currentIndex: sessionIndex
        textRole: "name"
        onCurrentIndexChanged: sessionIndex = currentIndex

        background: Rectangle {
            color: "#0f1726"; radius: 5
            border { color: "#2c3c58"; width: 1 }
        }
        contentItem: Text {
            leftPadding: 12
            text: sessionCombo.displayText
            font { pixelSize: 13; family: "Noto Sans" }
            color: "#8a99ba"
            verticalAlignment: Text.AlignVCenter
        }
    }

    // ── Power buttons — bottom right ──────────────────────────────────────────
    Row {
        anchors { bottom: parent.bottom; right: parent.right; margins: 36 }
        spacing: 10

        Button {
            width: 38; height: 38
            ToolTip.text: "Reboot"; ToolTip.visible: hovered; ToolTip.delay: 600
            background: Rectangle {
                color: parent.hovered ? "#1e2a42" : "transparent"
                radius: 5; border { color: "#2c3c58"; width: 1 }
            }
            contentItem: Text {
                text: "↺"; font.pixelSize: 20; color: "#8a99ba"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment:   Text.AlignVCenter
            }
            onClicked: sddm.reboot()
        }

        Button {
            width: 38; height: 38
            ToolTip.text: "Shutdown"; ToolTip.visible: hovered; ToolTip.delay: 600
            background: Rectangle {
                color: parent.hovered ? "#1e2a42" : "transparent"
                radius: 5; border { color: "#2c3c58"; width: 1 }
            }
            contentItem: Text {
                text: "⏻"; font.pixelSize: 17; color: "#8a99ba"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment:   Text.AlignVCenter
            }
            onClicked: sddm.powerOff()
        }
    }

    // ── Focus + auth logic ────────────────────────────────────────────────────
    function doLogin() {
        if (usernameField.text === "") { usernameField.forceActiveFocus(); return }
        sddm.login(usernameField.text, passwordField.text, sessionIndex)
    }

    Component.onCompleted: {
        if (userModel.lastUser !== "")
            passwordField.forceActiveFocus()
        else
            usernameField.forceActiveFocus()
    }

    Connections {
        target: sddm
        function onLoginFailed() {
            errorMsg.text = "Incorrect username or password"
            passwordField.text = ""
            passwordField.forceActiveFocus()
        }
    }
}
