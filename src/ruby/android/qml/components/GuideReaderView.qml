import QtQuick
import QtQuick.Controls
import ".."

Rectangle {
    id: root
    anchors.fill: parent
    color: Theme.surface0
    visible: opacity > 0
    opacity: 0

    property string guideId: ""
    property string guideTitle: ""
    property string guideCategory: ""
    property string guideSvg: ""
    property string markdownContent: ""
    property int fontDelta: 0

    signal closed()

    Behavior on opacity { NumberAnimation { duration: Theme.durMed; easing.type: Easing.OutCubic } }

    function open(id, title, category, svg, qrcPath) {
        guideId = id
        guideTitle = title
        guideCategory = category
        guideSvg = svg
        fontDelta = 0
        if (typeof rubyToolsBridge !== "undefined" && rubyToolsBridge) {
            markdownContent = rubyToolsBridge.loadDocMarkdown(qrcPath ? qrcPath : id)
        } else if (typeof toolsBridge !== "undefined" && toolsBridge) {
            markdownContent = toolsBridge.loadDocMarkdown(qrcPath ? qrcPath : id)
        } else {
            markdownContent = "# " + title + "\n\nDocumentation loaded in offline preview."
        }
        flickable.contentY = 0
        opacity = 1
    }

    function close() {
        opacity = 0
        closed()
    }

    Column {
        anchors.fill: parent

        // ── Top Header Navigation Bar ───────────────────────────────────
        Rectangle {
            id: headerBar
            width: parent.width
            height: Theme.dp(56)
            color: Theme.surface1
            border.color: Theme.borderSubtle
            border.width: 1

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.dp(Theme.spacingMd)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.dp(Theme.spacingSm)

                IconButton {
                    iconName: "arrow-left"
                    variant: "soft"
                    buttonSize: Theme.dp(40)
                    iconSize: Theme.dp(20)
                    square: true
                    onClicked: root.close()
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.width - Theme.dp(210)
                    spacing: Theme.dp(1)

                    Text {
                        width: parent.width
                        text: root.guideTitle
                        font.pixelSize: Theme.dp(15)
                        font.weight: Font.Bold
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                    }

                    Row {
                        spacing: Theme.dp(6)
                        Rectangle {
                            height: Theme.dp(16)
                            width: catTag.implicitWidth + Theme.dp(8)
                            radius: Theme.radiusPill
                            color: Theme.alpha(Theme.accentInk, 0.14)

                            Text {
                                id: catTag
                                anchors.centerIn: parent
                                text: root.guideCategory
                                font.pixelSize: Theme.dp(9)
                                font.weight: Font.Bold
                                color: Theme.accentInk
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Swordigo Modding Guide")
                            font.pixelSize: Theme.dp(10)
                            color: Theme.textMuted
                        }
                    }
                }
            }

            // Right Font Zoom Buttons
            Row {
                anchors.right: parent.right
                anchors.rightMargin: Theme.dp(Theme.spacingMd)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.dp(4)

                Rectangle {
                    width: Theme.dp(36)
                    height: Theme.dp(36)
                    radius: Theme.radiusSm
                    color: zoomOutMouse.pressed ? Theme.surface2 : Theme.surface1
                    border.color: Theme.borderSubtle
                    border.width: 1
                    opacity: root.fontDelta > -3 ? 1.0 : 0.4

                    Text {
                        anchors.centerIn: parent
                        text: "A-"
                        font.pixelSize: Theme.dp(13)
                        font.weight: Font.Bold
                        color: Theme.textSecondary
                    }

                    MouseArea {
                        id: zoomOutMouse
                        anchors.fill: parent
                        enabled: root.fontDelta > -3
                        onClicked: root.fontDelta = Math.max(-3, root.fontDelta - 1)
                    }
                }

                Rectangle {
                    width: Theme.dp(36)
                    height: Theme.dp(36)
                    radius: Theme.radiusSm
                    color: zoomInMouse.pressed ? Theme.surface2 : Theme.surface1
                    border.color: Theme.borderSubtle
                    border.width: 1
                    opacity: root.fontDelta < 5 ? 1.0 : 0.4

                    Text {
                        anchors.centerIn: parent
                        text: "A+"
                        font.pixelSize: Theme.dp(13)
                        font.weight: Font.Bold
                        color: Theme.textSecondary
                    }

                    MouseArea {
                        id: zoomInMouse
                        anchors.fill: parent
                        enabled: root.fontDelta < 5
                        onClicked: root.fontDelta = Math.min(5, root.fontDelta + 1)
                    }
                }
            }
        }

        // ── Scrollable Markdown Reader Content ──────────────────────────
        Flickable {
            id: flickable
            width: parent.width
            height: parent.height - headerBar.height
            contentHeight: readerCol.implicitHeight + Theme.dp(Theme.spacingXl) * 2
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: readerCol
                x: Theme.dp(Theme.paddingScreen)
                width: parent.width - Theme.dp(Theme.paddingScreen) * 2
                topPadding: Theme.dp(Theme.spacingLg)
                spacing: Theme.dp(Theme.spacingLg)

                // Optional SVG Graphic Banner
                Rectangle {
                    width: parent.width
                    height: Theme.dp(160)
                    radius: Theme.radiusCard
                    color: "#141721"
                    border.color: Theme.border
                    border.width: 1
                    clip: true
                    visible: root.guideSvg.length > 0

                    Image {
                        anchors.fill: parent
                        source: root.guideSvg
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                    }

                    // Ambient subtle vignette overlay
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "transparent" }
                            GradientStop { position: 1.0; color: "#AA0F1117" }
                        }
                    }
                }

                // Styled Markdown Viewer
                TextEdit {
                    id: mdViewer
                    width: parent.width
                    readOnly: true
                    selectByMouse: true
                    textFormat: TextEdit.MarkdownText
                    wrapMode: TextEdit.Wrap
                    color: Theme.textPrimary
                    font.pixelSize: Theme.dp(Theme.fontMd + root.fontDelta)
                    font.family: Theme.fontFamily
                    text: root.markdownContent

                    // Custom styling for links
                    onLinkActivated: (link) => {
                        Qt.openUrlExternally(link)
                    }
                }

                // Footer End-of-Guide Pill
                Rectangle {
                    width: parent.width
                    height: Theme.dp(48)
                    radius: Theme.radiusMd
                    color: Theme.surface1
                    border.color: Theme.borderSubtle
                    border.width: 1

                    Row {
                        anchors.centerIn: parent
                        spacing: Theme.dp(8)

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "check"
                            size: Theme.dp(16)
                            color: "#10B981"
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("End of Guide • Happy Modding!")
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            font.weight: Font.Medium
                            color: Theme.textSecondary
                        }
                    }
                }
            }
        }
    }
}
