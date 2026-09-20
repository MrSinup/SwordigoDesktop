import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    height: Theme.dp(56)

    property string userName: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.userName : "Ruby Dev"
    property string userTitle: (typeof rubySettings !== "undefined" && rubySettings && rubySettings.userTitle) ? rubySettings.userTitle : "DEV"
    property string workspaceName: "Local Ruby Workspace"
    property string statusText: qsTr("Local Active")
    property bool isConnected: true

    signal profileClicked()
    signal settingsClicked()
    signal notificationsClicked()

    Row {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.dp(Theme.spacingMd)

        // ── Avatar Monogram with Ruby Halo ──────────────────────────────
        Item {
            width: Theme.dp(44)
            height: Theme.dp(44)
            anchors.verticalCenter: parent.verticalCenter

            // Ambient Ruby Glow
            Rectangle {
                anchors.centerIn: parent
                width: parent.width + Theme.dp(4)
                height: parent.height + Theme.dp(4)
                radius: width / 2
                color: Theme.alpha(Theme.accentStart, 0.22)
            }

            // Outer Facet Ring
            Rectangle {
                id: avatarRing
                anchors.fill: parent
                radius: width / 2
                color: "#18141D"
                border.color: Theme.accentEnd
                border.width: Theme.dp(1.5)

                Text {
                    anchors.centerIn: parent
                    text: root.userName.length > 0 ? root.userName.charAt(0).toUpperCase() : "Q"
                    font.pixelSize: Theme.dp(18)
                    font.weight: Font.Bold
                    font.family: Theme.monoFamily
                    color: Theme.accentInk
                }
            }

            // Local Status Indicator Dot
            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: Theme.dp(1)
                anchors.bottomMargin: Theme.dp(1)
                width: Theme.dp(11)
                height: Theme.dp(11)
                radius: width / 2
                color: "#10B981"
                border.color: Theme.surface0
                border.width: Theme.dp(2)
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.profileClicked()
            }
        }

        // ── User Identity Text Column ───────────────────────────────────
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.dp(2)

            Row {
                spacing: Theme.dp(6)
                Text {
                    text: root.userName
                    font.pixelSize: Theme.dp(15)
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    height: Theme.dp(16)
                    width: proTag.implicitWidth + Theme.dp(8)
                    radius: Theme.radiusPill
                    color: Theme.alpha(Theme.accentInk, 0.12)
                    border.color: Theme.alpha(Theme.accentInk, 0.28)
                    border.width: 1

                    Text {
                        id: proTag
                        anchors.centerIn: parent
                        text: root.userTitle
                        font.pixelSize: Theme.dp(9)
                        font.weight: Font.Bold
                        color: Theme.accentInk
                    }
                }
            }

            Row {
                spacing: Theme.dp(4)
                anchors.left: parent.left

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "folder"
                    size: Theme.dp(11)
                    color: Theme.textMuted
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.workspaceName
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textSecondary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "•"
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textMuted
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.statusText
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: "#34D399"
                    font.weight: Font.Medium
                }
            }
        }
    }

    // ── Right Action Icons ──────────────────────────────────────────────
    Row {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.dp(Theme.spacingXs)

        IconButton {
            iconName: "settings"
            variant: "soft"
            buttonSize: Theme.dp(36)
            iconSize: Theme.dp(18)
            square: true
            onClicked: root.settingsClicked()
        }
    }
}
