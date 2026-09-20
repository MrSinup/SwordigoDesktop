import QtQuick
import QtQuick.Controls
import ".."

// Themed confirm dialog. Replaces the four Dialog blocks that were duplicated
// across the views, each with its own untuned standard buttons.
Popup {
    id: root

    property string title: qsTr("Confirm")
    property string message: ""
    property string confirmText: qsTr("Confirm")
    property string cancelText: qsTr("Cancel")
    property bool danger: false

    signal confirmed()
    signal cancelled()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - Theme.dp(48) : Theme.dp(320), Theme.dp(360))
    height: content.implicitHeight + Theme.dp(28)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0

    Overlay.modal: Rectangle { color: "#99000000" }

    background: Rectangle {
        radius: Theme.radiusSheet
        color: Theme.surface1
        border.color: Theme.border
        border.width: 1
    }

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Theme.dp(Theme.spacingLg)
        spacing: Theme.dp(Theme.spacingMd)

        Row {
            width: parent.width
            spacing: Theme.dp(Theme.spacingMd)

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.dp(36)
                height: Theme.dp(36)
                radius: Theme.radiusMd
                visible: root.danger
                color: Theme.alpha(Theme.colorError, 0.30)
                border.color: Theme.alpha(Theme.colorErrorBright, 0.45)
                border.width: 1

                Icon {
                    anchors.centerIn: parent
                    name: "alert"
                    size: Theme.dp(Theme.iconMd)
                    color: Theme.colorErrorBright
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - (root.danger ? Theme.dp(48) : 0)
                spacing: Theme.dp(4)

                Text {
                    width: parent.width
                    text: root.title
                    font.pixelSize: Theme.dp(Theme.fontXl)
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: root.message
                    font.pixelSize: Theme.dp(Theme.fontMd)
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                }
            }
        }

        Row {
            width: parent.width
            spacing: Theme.dp(Theme.spacingSm)
            layoutDirection: Qt.RightToLeft

            Rectangle {
                width: Theme.dp(96)
                height: Theme.dp(40)
                radius: Theme.radiusSm
                color: confirmArea.pressed
                       ? (root.danger ? Theme.colorError : Theme.accentStart)
                       : (root.danger ? Theme.alpha(Theme.colorError, 0.85) : Theme.accentStart)
                border.color: root.danger ? Theme.alpha(Theme.colorErrorBright, 0.5) : Theme.accentEnd
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: root.confirmText
                    font.pixelSize: Theme.dp(Theme.fontMd)
                    font.weight: Font.DemiBold
                    color: Theme.onAccent
                }

                MouseArea {
                    id: confirmArea
                    anchors.fill: parent
                    onClicked: {
                        root.close()
                        root.confirmed()
                    }
                }
            }

            Rectangle {
                width: Theme.dp(96)
                height: Theme.dp(40)
                radius: Theme.radiusSm
                color: cancelArea.pressed ? Theme.surface2 : "transparent"
                border.color: Theme.borderStrong
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: root.cancelText
                    font.pixelSize: Theme.dp(Theme.fontMd)
                    color: Theme.textSecondary
                }

                MouseArea {
                    id: cancelArea
                    anchors.fill: parent
                    onClicked: {
                        root.close()
                        root.cancelled()
                    }
                }
            }
        }
    }
}
