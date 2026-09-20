import QtQuick
import QtQuick.Controls
import ".."

// Files list row. Public contract preserved exactly:
//   fileName / filePath / fileType, fileSizeStr|fileSizeText,
//   fileDateStr|fileDateText, isDir|isDirectory, isPinned,
//   rowClicked / rowLongPressed / actionRequested(string) /
//   pinToggled / renameClicked / deleteClicked
SwipeDelegate {
    id: root

    width: ListView.view ? ListView.view.width : parent.width
    height: Theme.dp(Theme.rowHeight)

    property string fileName: ""
    property string filePath: ""
    property string fileType: ""
    property string fileSizeText: ""
    property string fileDateText: ""
    property bool isDirectory: false
    property alias fileSizeStr: root.fileSizeText
    property alias fileDateStr: root.fileDateText
    property alias isDir: root.isDirectory
    property bool isPinned: false

    signal rowClicked()
    signal rowLongPressed()
    signal actionRequested(string action)
    signal pinToggled()
    signal renameClicked()
    signal deleteClicked()

    readonly property color typeColor: Theme.colorForType(root.fileType)
    readonly property string typeIcon: root.isDirectory ? "folder" : Theme.iconForType(root.fileType)

    background: Rectangle {
        color: root.down ? Theme.surface2 : Theme.surface1
        border.color: Theme.borderSubtle
        border.width: 1

        // 3dp semantic data-type indicator bar on the left edge
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            color: root.typeColor
        }
    }

    leftPadding: Theme.dp(Theme.spacingLg)
    rightPadding: Theme.dp(Theme.spacingSm)

    contentItem: Row {
        spacing: Theme.dp(Theme.spacingMd)

        // Type plate + vector icon
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.dp(38)
            height: Theme.dp(38)
            radius: Theme.radiusMd
            color: Theme.alpha(root.typeColor, 0.14)
            border.color: Theme.alpha(root.typeColor, 0.28)
            border.width: 1

            Icon {
                anchors.centerIn: parent
                name: root.typeIcon
                size: Theme.dp(Theme.iconMd)
                color: root.typeColor
            }
        }

        // Title / meta
        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - Theme.dp(38) - Theme.dp(Theme.spacingMd) - Theme.dp(36) - Theme.dp(Theme.spacingMd)
            spacing: Theme.dp(2)

            Row {
                width: parent.width
                spacing: Theme.dp(5)

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.isPinned
                    name: "pin"
                    size: Theme.dp(12)
                    color: Theme.colorWarning
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - (root.isPinned ? Theme.dp(17) : 0)
                    text: root.fileName
                    font.pixelSize: Theme.dp(Theme.fontLg)
                    font.weight: root.isDirectory ? Font.DemiBold : Font.Normal
                    color: Theme.textPrimary
                    elide: Text.ElideMiddle
                }
            }

            Text {
                width: parent.width
                text: root.isDirectory
                      ? qsTr("Folder")
                      : (root.fileSizeText + "  ·  " + root.fileDateText)
                font.pixelSize: Theme.dp(Theme.fontSm)
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
        }

        // Per-row overflow menu
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: "more-vertical"
            buttonSize: Theme.dp(36)
            iconSize: Theme.dp(Theme.iconSm)
            onClicked: root.actionRequested("menu")
        }
    }

    // Leading swipe: pin / unpin
    swipe.left: Rectangle {
        width: Theme.dp(76)
        height: parent.height
        color: root.isPinned ? Theme.colorWarning : Theme.accentStart
        anchors.left: parent.left

        Column {
            anchors.centerIn: parent
            spacing: Theme.dp(2)

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "pin"
                size: Theme.dp(Theme.iconMd)
                color: root.isPinned ? "#2A1B00" : Theme.onAccent
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.isPinned ? qsTr("Unpin") : qsTr("Pin")
                color: root.isPinned ? "#2A1B00" : Theme.onAccent
                font.pixelSize: Theme.dp(Theme.fontSm)
                font.weight: Font.DemiBold
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                root.swipe.close()
                root.actionRequested("pin")
                root.pinToggled()
            }
        }
    }

    // Trailing swipe: rename / delete
    swipe.right: Row {
        anchors.right: parent.right
        height: parent.height

        Rectangle {
            width: Theme.dp(64)
            height: parent.height
            color: Theme.accentEnd

            Column {
                anchors.centerIn: parent
                spacing: Theme.dp(2)
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "pencil"
                    size: Theme.dp(Theme.iconSm)
                    color: Theme.onAccent
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Rename")
                    color: Theme.onAccent
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    font.weight: Font.DemiBold
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    root.swipe.close()
                    root.actionRequested("rename")
                    root.renameClicked()
                }
            }
        }

        Rectangle {
            width: Theme.dp(64)
            height: parent.height
            color: Theme.colorError

            Column {
                anchors.centerIn: parent
                spacing: Theme.dp(2)
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "trash"
                    size: Theme.dp(Theme.iconSm)
                    color: Theme.onAccent
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Delete")
                    color: Theme.onAccent
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    font.weight: Font.DemiBold
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    root.swipe.close()
                    root.actionRequested("delete")
                    root.deleteClicked()
                }
            }
        }
    }

    onClicked: root.rowClicked()
    onPressAndHold: root.rowLongPressed()
}
