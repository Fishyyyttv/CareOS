import QtQuick 2.15
import calamares.slideshow 1.0

Presentation {
    id: presentation

    function nextSlide() { presentation.goToNextSlide() }
    Timer { interval: 6500; running: true; repeat: true; onTriggered: nextSlide() }

    Slide {
        Rectangle {
            anchors.fill: parent
            color: "#080d17"
            Rectangle { width: parent.width; height: 5; color: "#559aff" }
            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width * 0.74, 640)
                spacing: 16
                Text { text: "CAREOS 2026.05"; color: "#559aff"; font.pixelSize: 13; font.bold: true; font.letterSpacing: 2; horizontalAlignment: Text.AlignHCenter; width: parent.width }
                Text { text: "CareOS is being installed"; color: "#eaf0ff"; font.pixelSize: 36; font.weight: Font.Light; horizontalAlignment: Text.AlignHCenter; width: parent.width }
                Rectangle { width: 72; height: 3; radius: 2; color: "#559aff"; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "Your new system is getting the CareOS identity, Plasma theme, shell profile, package tools, and Care Language environment."; color: "#8a99ba"; font.pixelSize: 16; lineHeight: 1.35; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; width: parent.width }
            }
        }
    }

    Slide {
        Rectangle {
            anchors.fill: parent
            color: "#080d17"
            Rectangle { width: parent.width; height: 5; color: "#2ecc8e" }
            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width * 0.74, 640)
                spacing: 16
                Text { text: "PLASMA EXPERIENCE"; color: "#2ecc8e"; font.pixelSize: 13; font.bold: true; font.letterSpacing: 2; horizontalAlignment: Text.AlignHCenter; width: parent.width }
                Text { text: "A desktop that feels like CareOS"; color: "#eaf0ff"; font.pixelSize: 36; font.weight: Font.Light; horizontalAlignment: Text.AlignHCenter; width: parent.width }
                Rectangle { width: 72; height: 3; radius: 2; color: "#2ecc8e"; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "The panel, wallpaper, color scheme, login screen, terminal profile, and welcome center are preconfigured for the CareOS session."; color: "#8a99ba"; font.pixelSize: 16; lineHeight: 1.35; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; width: parent.width }
            }
        }
    }

    Slide {
        Rectangle {
            anchors.fill: parent
            color: "#080d17"
            Rectangle { width: parent.width; height: 5; color: "#82bcff" }
            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width * 0.74, 640)
                spacing: 16
                Text { text: "ROLLING FOUNDATION"; color: "#82bcff"; font.pixelSize: 13; font.bold: true; font.letterSpacing: 2; horizontalAlignment: Text.AlignHCenter; width: parent.width }
                Text { text: "Arch core, CareOS surface"; color: "#eaf0ff"; font.pixelSize: 36; font.weight: Font.Light; horizontalAlignment: Text.AlignHCenter; width: parent.width }
                Rectangle { width: 72; height: 3; radius: 2; color: "#82bcff"; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "CareOS keeps the reliable Arch package base while presenting a cleaner CareOS command flow with carepkg, carectl, and careos-update."; color: "#8a99ba"; font.pixelSize: 16; lineHeight: 1.35; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; width: parent.width }
            }
        }
    }

    Slide {
        Rectangle {
            anchors.fill: parent
            color: "#080d17"
            Rectangle { width: parent.width; height: 5; color: "#f56060" }
            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width * 0.74, 640)
                spacing: 16
                Text { text: "FINAL SETUP"; color: "#f56060"; font.pixelSize: 13; font.bold: true; font.letterSpacing: 2; horizontalAlignment: Text.AlignHCenter; width: parent.width }
                Text { text: "Almost ready"; color: "#eaf0ff"; font.pixelSize: 36; font.weight: Font.Light; horizontalAlignment: Text.AlignHCenter; width: parent.width }
                Rectangle { width: 72; height: 3; radius: 2; color: "#f56060"; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "After reboot, sign in to the CareOS desktop and open CareOS Welcome or carectl to continue."; color: "#8a99ba"; font.pixelSize: 16; lineHeight: 1.35; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; width: parent.width }
            }
        }
    }
}
