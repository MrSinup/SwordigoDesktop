import QtQuick
import QtQuick.Controls
import ".."

// Quick-action tile (Home shortcuts, tool launchers). The icon sits on a
// tinted plate keyed to the action's semantic colour instead of a coloured
// emoji, so the whole grid reads as one system.
Rectangle {
    id: root

    property string iconName: ""
    property string title: ""
    property string subtitle: ""
    property color accent: Theme.accentEnd
    property bool compact: false

    signal clicked()

    implicitHeight: Theme.dp(root.compact ? 74 : 84)
    radius: Theme.radiusCard
    color: tileArea.pressed ? Theme.surface2 : Theme.surface1
    border.color: tileArea.pressed ? Theme.alpha(root.accent, 0.55) : Theme.borderSubtle
    border.width: 1

    Behavior on color { ColorAnimation { duration: Theme.durFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

    Row {
        anchors.fill: parent
        anchors.leftMargin: Theme.dp(Theme.spacingMd)
        anchors.rightMargin: Theme.dp(Theme.spacingMd)
        spacing: Theme.dp(Theme.spacingMd)

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.dp(40)
            height: Theme.dp(40)
            radius: Theme.radiusMd
            color: Theme.alpha(root.accent, 0.15)
            border.color: Theme.alpha(root.accent, 0.30)
            border.width: 1

            Icon {
                anchors.centerIn: parent
                name: root.iconName
                size: Theme.dp(Theme.iconMd)
                color: root.accent
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - Theme.dp(40) - Theme.dp(Theme.spacingMd)
            spacing: Theme.dp(2)

            Text {
                width: parent.width
                text: root.title
                font.pixelSize: Theme.dp(Theme.fontMd)
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: root.subtitle.length > 0
                text: root.subtitle
                font.pixelSize: Theme.dp(Theme.fontSm)
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
        }
    }

    MouseArea {
        id: tileArea
        anchors.fill: parent
        onClicked: root.clicked()
    }
}
