import QtQuick
import QtQuick.Controls
import ".."

// Settings list row: label + optional description on the left, and either a
// Switch, a trailing value, or a chevron on the right.
Item {
    id: root

    property string title: ""
    property string description: ""
    property string leadingIcon: ""
    property bool hasSwitch: false
    property bool checked: false
    property bool hasChevron: false
    property string trailingText: ""

    signal toggled(bool checked)
    signal clicked()

    implicitHeight: description.length > 0 ? Theme.dp(58) : Theme.dp(50)
    width: parent ? parent.width : implicitWidth

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSm
        color: rowArea.pressed ? Theme.alpha(Theme.textPrimary, 0.045) : "transparent"
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.dp(Theme.spacingSm)
        anchors.right: parent.right
        anchors.rightMargin: Theme.dp(Theme.spacingSm)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.dp(Theme.spacingMd)

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.leadingIcon.length > 0
            name: root.leadingIcon
            size: Theme.dp(Theme.iconSm)
            color: Theme.textMuted
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - Theme.dp(52)
            spacing: Theme.dp(1)

            Text {
                width: parent.width
                text: root.title
                font.pixelSize: Theme.dp(Theme.fontMd)
                color: Theme.textPrimary
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: root.description.length > 0
                text: root.description
                font.pixelSize: Theme.dp(Theme.fontSm)
                color: Theme.textMuted
                wrapMode: Text.WordWrap
            }
        }
    }

    Text {
        anchors.right: trailing.left
        anchors.rightMargin: Theme.dp(Theme.spacingSm)
        anchors.verticalCenter: parent.verticalCenter
        visible: root.trailingText.length > 0
        text: root.trailingText
        font.pixelSize: Theme.dp(Theme.fontSm)
        color: Theme.textMuted
    }

    Icon {
        id: trailing
        anchors.right: parent.right
        anchors.rightMargin: Theme.dp(Theme.spacingMd)
        anchors.verticalCenter: parent.verticalCenter
        visible: root.hasChevron
        name: "chevron-right"
        size: Theme.dp(Theme.iconSm)
        color: Theme.textMuted
    }

    Switch {
        id: switchControl
        visible: root.hasSwitch
        checked: root.checked
        anchors.right: parent.right
        anchors.rightMargin: Theme.dp(Theme.spacingSm)
        anchors.verticalCenter: parent.verticalCenter
        onToggled: root.toggled(checked)
    }

    MouseArea {
        id: rowArea
        anchors.fill: parent
        anchors.rightMargin: root.hasSwitch ? Theme.dp(60) : 0
        onClicked: root.clicked()
    }
}
