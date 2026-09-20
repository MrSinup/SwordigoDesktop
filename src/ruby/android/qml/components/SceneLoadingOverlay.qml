import QtQuick
import QtQuick.Controls
import ".."

// ============================================================================
// SceneLoadingOverlay.qml — Smooth loading screen for Scene Viewport
// Displays a pulsing Ruby gem emblem, scene title, and indeterminate progress bar
// during scene load and orientation transitions to eliminate intermediate glitch stages.
// ============================================================================
Rectangle {
    id: root

    property bool loading: false
    property string sceneName: ""
    property string statusText: qsTr("Loading 3D Viewport...")

    color: Theme.surface0
    visible: opacity > 0.0
    opacity: loading ? 1.0 : 0.0

    Behavior on opacity {
        NumberAnimation { duration: 250; easing.type: Easing.OutQuad }
    }

    // Touch absorber so user cannot interact while loading
    MouseArea {
        anchors.fill: parent
        preventStealing: true
    }

    // Subtle ambient glow
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 0.7
        height: width
        radius: width / 2
        color: Theme.alpha(Theme.accentStart, 0.08)
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.dp(Theme.spacingMd)
        width: Math.min(parent.width - Theme.dp(48), Theme.dp(320))

        // Pulsing Gem Emblem
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.dp(72)
            height: Theme.dp(72)

            Rectangle {
                id: halo
                anchors.centerIn: parent
                width: Theme.dp(68)
                height: Theme.dp(68)
                radius: Theme.radiusMd
                color: Theme.alpha(Theme.accentStart, 0.25)

                SequentialAnimation on scale {
                    running: root.loading
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.95; to: 1.15; duration: 900; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 1.15; to: 0.95; duration: 900; easing.type: Easing.InOutSine }
                }

                SequentialAnimation on opacity {
                    running: root.loading
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.4; to: 0.85; duration: 900; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 0.85; to: 0.4; duration: 900; easing.type: Easing.InOutSine }
                }
            }

            Rectangle {
                anchors.centerIn: parent
                width: Theme.dp(56)
                height: Theme.dp(56)
                radius: Theme.radiusMd
                color: Theme.surface1
                border.color: Theme.alpha(Theme.accentEnd, 0.6)
                border.width: 1

                Icon {
                    anchors.centerIn: parent
                    name: "ruby"
                    size: Theme.dp(30)
                    color: Theme.accentInk
                }
            }
        }

        // Scene Title
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.sceneName.length > 0 ? root.sceneName : qsTr("Loading Scene")
            font.pixelSize: Theme.dp(Theme.fontTitle)
            font.weight: Font.Bold
            color: Theme.textPrimary
            elide: Text.ElideMiddle
        }

        // Status Subtitle
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.statusText
            font.pixelSize: Theme.dp(Theme.fontSm)
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }

        Item { width: 1; height: Theme.dp(4) }

        // Indeterminate Progress Bar
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.dp(160)
            height: Theme.dp(4)
            radius: Theme.dp(2)
            color: Theme.surface2
            clip: true

            Rectangle {
                id: indicator
                width: Theme.dp(48)
                height: parent.height
                radius: parent.radius
                color: Theme.accentInk

                SequentialAnimation on x {
                    running: root.loading
                    loops: Animation.Infinite
                    NumberAnimation {
                        from: -indicator.width
                        to: indicator.parent.width
                        duration: 1100
                        easing.type: Easing.InOutQuad
                    }
                }
            }
        }
    }
}
