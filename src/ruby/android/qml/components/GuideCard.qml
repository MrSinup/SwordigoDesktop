import QtQuick
import QtQuick.Controls
import ".."

Rectangle {
    id: root

    width: Theme.dp(245)
    height: Theme.dp(218)
    radius: Theme.radiusCard
    color: Theme.surface1
    border.color: mouseArea.pressed ? Theme.accentEnd : Theme.border
    border.width: 1
    clip: true

    property string title: ""
    property string category: ""
    property string tag: ""
    property string description: ""
    property string readTime: "4 min read"
    property string svgSource: ""
    property string qrcPath: ""

    signal clicked()

    Behavior on border.color { ColorAnimation { duration: 150 } }

    Column {
        anchors.fill: parent

        // ── Top SVG Visual Banner ───────────────────────────────────────
        Item {
            width: parent.width
            height: Theme.dp(105)
            clip: true

            Rectangle {
                anchors.fill: parent
                color: "#12141A"
            }

            Image {
                anchors.fill: parent
                source: root.svgSource
                fillMode: Image.PreserveAspectCrop
                smooth: true
                asynchronous: true
            }

            // Bottom Gradient Scrim for text readability
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.dp(30)
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Theme.surface1 }
                }
            }

            // Category Badge Floating Pill
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: Theme.dp(8)
                height: Theme.dp(18)
                width: catText.implicitWidth + Theme.dp(12)
                radius: Theme.radiusPill
                color: "#CC0F1117"
                border.color: Theme.alpha(Theme.accentInk, 0.5)
                border.width: 1

                Text {
                    id: catText
                    anchors.centerIn: parent
                    text: root.category
                    font.pixelSize: Theme.dp(9)
                    font.weight: Font.Bold
                    font.letterSpacing: 0.5
                    color: Theme.accentInk
                }
            }
        }

        // ── Bottom Content Area ─────────────────────────────────────────
        Column {
            width: parent.width
            padding: Theme.dp(10)
            spacing: Theme.dp(4)

            Text {
                width: parent.width - Theme.dp(20)
                text: root.title
                font.pixelSize: Theme.dp(14)
                font.weight: Font.Bold
                color: Theme.textPrimary
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Text {
                width: parent.width - Theme.dp(20)
                text: root.description
                font.pixelSize: Theme.dp(11)
                color: Theme.textSecondary
                elide: Text.ElideRight
                maximumLineCount: 2
                wrapMode: Text.WordWrap
                lineHeight: 1.15
            }

            Item { height: Theme.dp(2); width: 1 }

            // Read Time & Explore Action
            Row {
                width: parent.width - Theme.dp(20)
                spacing: Theme.dp(6)

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.readTime
                    font.pixelSize: Theme.dp(10)
                    color: Theme.textMuted
                }

                Item { width: Theme.dp(4); height: 1 }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "•"
                    font.pixelSize: Theme.dp(10)
                    color: Theme.textMuted
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Read guide →")
                    font.pixelSize: Theme.dp(11)
                    font.weight: Font.DemiBold
                    color: mouseArea.pressed ? Theme.accentEnd : Theme.accentInk
                }
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
