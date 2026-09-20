import QtQuick
import QtQuick.Controls
import ".."

// Shared app bar for the deep views (code editor, scene viewport, texture
// viewer). One implementation instead of the three near-identical
// Rectangle+Row headers that were previously copy-pasted.
//
// Right-hand actions go into the default property.
Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    property bool showBack: true
    property color subtitleColor: Theme.textMuted
    property Item leftExtra: null

    signal backClicked()

    default property alias actions: actionsRow.data

    readonly property real safeTop: (typeof androidContext !== "undefined" && androidContext) ? androidContext.safeInsetTop : 0
    height: Theme.dp(Theme.topBarHeight) + safeTop
    color: Theme.surface1

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.borderSubtle
    }

    Item {
        id: barContent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.dp(Theme.topBarHeight)

        IconButton {
            id: backButton
            visible: root.showBack
            anchors.left: parent.left
            anchors.leftMargin: Theme.dp(Theme.spacingSm)
            anchors.verticalCenter: parent.verticalCenter
            iconName: "arrow-left"
            iconSize: Theme.dp(20)
            onClicked: root.backClicked()
        }

        Item {
            id: leftExtraSlot
            visible: root.leftExtra !== null
            anchors.left: root.showBack ? backButton.right : parent.left
            anchors.leftMargin: root.showBack ? Theme.dp(Theme.spacingXs) : Theme.dp(Theme.spacingMd)
            anchors.verticalCenter: parent.verticalCenter
            width: visible ? childrenRect.width : 0
            height: parent.height
        }

        Row {
            id: actionsRow
            anchors.right: parent.right
            anchors.rightMargin: Theme.dp(Theme.spacingSm)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.dp(Theme.spacingXs)
        }

        Column {
            anchors.left: leftExtraSlot.right
            anchors.leftMargin: Theme.dp(Theme.spacingSm)
            anchors.right: actionsRow.left
            anchors.rightMargin: Theme.dp(Theme.spacingSm)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.dp(1)

            Text {
                width: parent.width
                text: root.title
                font.pixelSize: Theme.dp(Theme.fontLg)
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                elide: Text.ElideMiddle
            }

            Text {
                width: parent.width
                visible: root.subtitle.length > 0
                text: root.subtitle
                font.pixelSize: Theme.dp(Theme.fontXs)
                color: root.subtitleColor
                elide: Text.ElideRight
            }
        }
    }
}
