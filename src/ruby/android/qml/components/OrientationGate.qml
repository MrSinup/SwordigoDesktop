import QtQuick
import QtQuick.Controls
import ".."

// ============================================================================
// OrientationGate.qml — the landscape-only guard for the scene editor.
//
// WHY
//   The studio is designed for landscape and only landscape: two NavPads, a
//   gizmo pad, a full-height outliner and a full-height inspector cannot share a
//   portrait phone screen without all four becoming unusable.
//   SceneOrientation forces landscape on Android, but that can still fail:
//   the user may have rotation locked, the device may be a desktop dev build
//   where Q_OS_ANDROID is false, or a window may simply be resized. Rather than
//   lay the studio out wrong, the editor covers itself with this card and says
//   exactly what to do.
//
// It is an overlay, not a replacement screen: the caller keeps the studio
// mounted underneath so nothing is torn down or reloaded when the device
// rotates back.
// ============================================================================
Rectangle {
    id: root

    /// True when the usable area is portrait (or too narrow to host the deck).
    property bool blocking: true
    /// Whether the platform can actually rotate for us.
    property bool platformCanRotate: false
    property string targetName: ""

    // Opaque: the studio underneath is not merely dimmed, it is unusable.
    color: Theme.surface0
    visible: opacity > 0.0
    opacity: blocking ? 1.0 : 0.0

    Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

    // Faint schematic of the landscape studio, so the instruction reads as a
    // promise of what is coming rather than an error.
    Item {
        id: diagram
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -Theme.dp(44)
        width: Theme.dp(148)
        height: Theme.dp(84)

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            color: "transparent"
            border.color: Theme.borderStrong
            border.width: 2
        }

        // The two pads the studio is built around
        Rectangle {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: Theme.dp(8)
            width: Theme.dp(26)
            height: Theme.dp(26)
            radius: 13
            color: Theme.alpha(Theme.accentInk, 0.22)
            border.color: Theme.alpha(Theme.accentInk, 0.55)
            border.width: 1
        }

        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.dp(8)
            width: Theme.dp(26)
            height: Theme.dp(26)
            radius: 13
            color: Theme.alpha(Theme.colorWarning, 0.20)
            border.color: Theme.alpha(Theme.colorWarning, 0.55)
            border.width: 1
        }

        // The gizmo grid between them
        Grid {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.dp(10)
            columns: 3
            spacing: Theme.dp(3)

            Repeater {
                model: 9

                delegate: Rectangle {
                    width: Theme.dp(12)
                    height: Theme.dp(12)
                    radius: Theme.radiusXs
                    color: index === 4 ? Theme.alpha(Theme.accentInk, 0.30)
                                       : Theme.alpha(Theme.textMuted, 0.16)
                }
            }
        }
    }

    // Rotate glyph, drawn rotating so the gesture is unmistakable
    Icon {
        id: rotateGlyph
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: diagram.top
        anchors.bottomMargin: Theme.dp(Theme.spacingLg)
        name: "refresh"
        size: Theme.dp(30)
        color: Theme.accentInk

        SequentialAnimation on rotation {
            running: root.blocking
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: 90; duration: 700; easing.type: Easing.InOutQuad }
            PauseAnimation { duration: 550 }
        }
    }

    signal backRequested()

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: diagram.bottom
        anchors.topMargin: Theme.dp(Theme.spacingLg)
        width: Math.min(parent.width - Theme.dp(48), Theme.dp(300))
        spacing: Theme.dp(Theme.spacingSm)

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Rotate to landscape")
            font.pixelSize: Theme.dp(Theme.fontTitle)
            font.weight: Font.DemiBold
            color: Theme.textPrimary
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.targetName.length > 0
                  ? qsTr("The scene editor for %1 is a landscape studio.").arg(root.targetName)
                  : qsTr("The scene editor is a landscape studio.")
            font.pixelSize: Theme.dp(Theme.fontMd)
            color: Theme.textSecondary
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: !root.platformCanRotate
            text: qsTr("Turn off rotation lock, or tap below to force landscape orientation.")
            font.pixelSize: Theme.dp(Theme.fontSm)
            color: Theme.textMuted
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.dp(Theme.spacingSm)
            topPadding: Theme.dp(Theme.spacingXs)

            Icon { anchors.verticalCenter: parent.verticalCenter; name: "camera"; size: Theme.dp(Theme.iconSm); color: Theme.textMuted }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Turn the device 90°")
                font.pixelSize: Theme.dp(Theme.fontSm)
                color: Theme.textMuted
            }
        }

        Item { width: 1; height: Theme.dp(8) }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            text: qsTr("Switch to Landscape")
            highlighted: true
            onClicked: {
                if (typeof screenOrientation !== "undefined" && screenOrientation)
                    screenOrientation.lockLandscape()
                if (typeof androidContext !== "undefined" && androidContext)
                    androidContext.lockLandscape()
            }
        }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            text: qsTr("Back to Menu")
            flat: true
            onClicked: root.backRequested()
        }
    }
}
