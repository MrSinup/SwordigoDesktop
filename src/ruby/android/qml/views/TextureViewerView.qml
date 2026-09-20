import QtQuick
import QtQuick.Controls
import ".."
import "../components"

Item {
    id: root

    property string filePath: ""
    property string fileName: ""
    property int imgWidth: 0
    property int imgHeight: 0
    property int imgSizeKb: 0
    property bool isPvr: false
    property int currentChannel: 0 // 0=RGBA, 1=Alpha, 2=Red, 3=Green, 4=Blue
    property real zoomScale: 1.0

    signal backRequested()

    onFilePathChanged: {
        if (filePath.length > 0) {
            textureBridge.loadTexture(filePath)
            root.updateFromBridge()
        }
    }

    function updateFromBridge() {
        root.fileName = textureBridge.fileName
        root.imgWidth = textureBridge.width
        root.imgHeight = textureBridge.height
        root.imgSizeKb = textureBridge.fileSizeKb
        root.isPvr = textureBridge.isPvr
        textureImage.source = ""
        textureImage.source = "image://ruby_pvr/" + root.filePath
                               + "?ch=" + root.currentChannel + "&t=" + Date.now()
    }

    readonly property var channels: ["RGBA", "Alpha", "Red", "Green", "Blue"]

    // ── App bar ────────────────────────────────────────────────────────────
    TopBar {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        title: root.fileName.length > 0 ? root.fileName : qsTr("Texture")
        subtitle: root.imgWidth + " × " + root.imgHeight
                  + "  ·  " + (root.isPvr ? qsTr("PVR RGBA8888") : qsTr("PNG / JPG"))
                  + (root.imgSizeKb > 0 ? "  ·  " + root.imgSizeKb + " KB" : "")

        onBackClicked: root.backRequested()

        PrimaryButton {
            width: Theme.dp(104)
            anchors.verticalCenter: parent.verticalCenter
            text: root.isPvr ? qsTr("Export PNG") : qsTr("Export PVR")
            iconName: "download"
            onClicked: {
                if (root.isPvr) {
                    textureBridge.exportPng()
                } else {
                    pvrSheet.open(false)
                }
            }
        }
    }

    // ── Image canvas ───────────────────────────────────────────────────────
    Flickable {
        id: flickable
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomControls.top
        contentWidth: Math.max(width, imageContainer.width * root.zoomScale)
        contentHeight: Math.max(height, imageContainer.height * root.zoomScale)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        // Alpha checkerboard, keyed to the theme so it does not glow
        Canvas {
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var sz = Theme.dp(14)
                ctx.fillStyle = Theme.surfaceSunken
                ctx.fillRect(0, 0, width, height)
                ctx.fillStyle = "#1B1F2A"
                for (var x = 0; x < width; x += sz) {
                    for (var y = 0; y < height; y += sz) {
                        if ((Math.floor(x / sz) + Math.floor(y / sz)) % 2 === 0) {
                            ctx.fillRect(x, y, sz, sz)
                        }
                    }
                }
            }
        }

        Item {
            id: imageContainer
            width: Math.max(64, root.imgWidth)
            height: Math.max(64, root.imgHeight)
            anchors.centerIn: parent
            scale: root.zoomScale

            Image {
                id: textureImage
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                smooth: root.zoomScale <= 2.0 // crisp pixels when zoomed in
                asynchronous: true
                cache: false
            }
        }

        PinchHandler {
            target: null
            onActiveScaleChanged: (delta) => {
                root.zoomScale = Math.max(0.1, Math.min(10.0, root.zoomScale * delta))
            }
        }
    }

    // Floating zoom badge — tap to reset
    GlassmorphicOverlay {
        anchors.top: topBar.bottom
        anchors.right: parent.right
        anchors.topMargin: Theme.dp(Theme.spacingMd)
        anchors.rightMargin: Theme.dp(Theme.spacingMd)
        width: Theme.dp(76)
        height: Theme.dp(30)

        Row {
            anchors.centerIn: parent
            spacing: Theme.dp(4)

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "zoom-in"
                size: Theme.dp(13)
                color: Theme.textSecondary
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Math.round(root.zoomScale * 100) + "%"
                font.pixelSize: Theme.dp(Theme.fontSm)
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.zoomScale = 1.0
        }
    }

    // ── Bottom controls ────────────────────────────────────────────────────
    Rectangle {
        id: bottomControls
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.dp(104)
        color: Theme.surface1

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.borderSubtle
        }

        Column {
            anchors.fill: parent
            anchors.margins: Theme.dp(Theme.spacingMd)
            spacing: Theme.dp(Theme.spacingMd)

            // Channel segmented control
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.dp(Theme.spacingXs)

                Repeater {
                    model: root.channels

                    Rectangle {
                        width: Theme.dp(60)
                        height: Theme.dp(30)
                        radius: Theme.radiusPill
                        color: root.currentChannel === index
                               ? Theme.accentStart : Theme.surface2
                        border.color: root.currentChannel === index
                                      ? Theme.accentEnd : Theme.borderSubtle
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            font.weight: root.currentChannel === index ? Font.DemiBold : Font.Normal
                            color: root.currentChannel === index ? Theme.onAccent : Theme.textSecondary
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                root.currentChannel = index
                                textureBridge.setChannel(index)
                                root.updateFromBridge()
                            }
                        }
                    }
                }
            }

            // Transform actions
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.dp(Theme.spacingSm)

                Repeater {
                    model: [
                        { icon: "rotate-ccw",      label: qsTr("Rot L"),  action: "rot_l"  },
                        { icon: "rotate",          label: qsTr("Rot R"),  action: "rot_r"  },
                        { icon: "flip-horizontal", label: qsTr("Flip H"), action: "flip_h" },
                        { icon: "flip-vertical",   label: qsTr("Flip V"), action: "flip_v" },
                        { icon: "undo",            label: qsTr("Undo"),   action: "undo"   }
                    ]

                    Rectangle {
                        width: Theme.dp(70)
                        height: Theme.dp(36)
                        radius: Theme.radiusSm
                        color: transformArea.pressed ? Theme.surface2 : "transparent"
                        border.color: Theme.borderSubtle
                        border.width: 1

                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.dp(5)

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: modelData.icon
                                size: Theme.dp(15)
                                color: Theme.accentInk
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                color: Theme.textSecondary
                            }
                        }

                        MouseArea {
                            id: transformArea
                            anchors.fill: parent
                            onClicked: {
                                switch (modelData.action) {
                                case "rot_l":  textureBridge.rotateLeft();    break
                                case "rot_r":  textureBridge.rotateRight();   break
                                case "flip_h": textureBridge.flipHorizontal(); break
                                case "flip_v": textureBridge.flipVertical();   break
                                case "undo":   textureBridge.undo();           break
                                }
                                root.updateFromBridge()
                            }
                        }
                    }
                }
            }
        }
    }

    // ── PVR export resolution picker ───────────────────────────────────────
    BottomSheet {
        id: pvrSheet
        title: qsTr("PVR Export Resolution")
        peekHeight: Theme.dp(240)

        Column {
            anchors.fill: parent
            anchors.margins: Theme.dp(Theme.spacingLg)
            spacing: Theme.dp(Theme.spacingMd)

            Row {
                width: parent.width
                spacing: Theme.dp(Theme.spacingSm)
                Repeater {
                    model: [
                        { label: "1×",  scale: 1.0  },
                        { label: "½",   scale: 0.5  },
                        { label: "¼",   scale: 0.25 },
                        { label: "2×",  scale: 2.0  }
                    ]
                    delegate: Chip {
                        text: modelData.label
                        onClicked: {
                            exportW.text = Math.round(root.imgWidth * modelData.scale)
                            exportH.text = Math.round(root.imgHeight * modelData.scale)
                        }
                    }
                }
            }

            Row {
                width: parent.width
                spacing: Theme.dp(Theme.spacingSm)

                ThemedField {
                    id: exportW
                    width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                    text: root.imgWidth
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "×"
                    color: Theme.textSecondary
                }

                ThemedField {
                    id: exportH
                    width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                    text: root.imgHeight
                }
            }

            PrimaryButton {
                width: parent.width
                text: qsTr("Export PVR")
                iconName: "download"
                onClicked: {
                    textureBridge.exportPvr(parseInt(exportW.text), parseInt(exportH.text))
                    pvrSheet.close()
                }
            }
        }
    }
}
