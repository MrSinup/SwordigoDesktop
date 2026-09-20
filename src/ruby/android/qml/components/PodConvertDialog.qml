import QtQuick
import QtQuick.Controls
import ".."

// ============================================================================
// PodConvertDialog.qml — 3D Model to Game POD Converter
// Mobile port of Ruby GG desktop's ModelConvertDialog.
// Converts GLB/GLTF/FBX/OBJ models into Swordigo-compatible .POD files with
// scale presets, auto-fit heights, and ETC1 PVR texture compression.
// ============================================================================
BottomSheet {
    id: root

    title: qsTr("Convert to Game POD")
    peekHeight: Theme.dp(600)

    property string sourcePath: ""
    property var modelInfo: null
    property bool isConverting: false
    property string conversionError: ""
    property string convertedPodPath: ""

    signal openConvertedModel(string podPath)

    function initForSource(path) {
        root.sourcePath = path
        root.isConverting = false
        root.conversionError = ""
        root.convertedPodPath = ""
        var parts = path.split("/")
        var fileName = parts[parts.length - 1]
        var base = fileName.substring(0, fileName.lastIndexOf("."))
        destField.text = base + ".pod"
        scaleField.text = "1.0"

        // Inspect model
        root.modelInfo = toolsBridge.inspectModel(path)
    }

    Connections {
        target: toolsBridge
        function onConversionStarted() {
            root.isConverting = true
            root.conversionError = ""
            root.convertedPodPath = ""
        }
        function onConversionFinished(success, outputPath, errorMessage) {
            root.isConverting = false
            if (success) {
                root.convertedPodPath = outputPath
            } else {
                root.conversionError = errorMessage || qsTr("Conversion failed.")
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: convertCol.implicitHeight + Theme.dp(Theme.spacingXl)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: convertCol
            width: parent.width
            spacing: Theme.dp(Theme.spacingMd)

            // Source File Card
            Rectangle {
                width: parent.width
                height: srcCol.implicitHeight + Theme.dp(20)
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.border
                border.width: 1

                Column {
                    id: srcCol
                    anchors.fill: parent
                    anchors.margins: Theme.dp(10)
                    spacing: Theme.dp(4)

                    Row {
                        spacing: Theme.dp(6)
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "cube"; size: Theme.dp(14); color: Theme.accentInk }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Source: %1").arg(root.sourcePath.split("/").pop())
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                            elide: Text.ElideMiddle
                            width: srcCol.width - Theme.dp(30)
                        }
                    }

                    // Geometry Specs Badge
                    Text {
                        width: parent.width
                        visible: root.modelInfo && root.modelInfo.valid === true
                        text: root.modelInfo ? qsTr("Bounds: %1W × %2H × %3D u  |  Meshes: %4  |  Verts: %5")
                                                .arg(root.modelInfo.width.toFixed(1))
                                                .arg(root.modelInfo.height.toFixed(1))
                                                .arg(root.modelInfo.depth.toFixed(1))
                                                .arg(root.modelInfo.meshCount)
                                                .arg(root.modelInfo.vertexCount)
                                             : ""
                        font.pixelSize: Theme.dp(11)
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Target POD Filename
            Rectangle {
                width: parent.width
                height: destCol.implicitHeight + Theme.dp(20)
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.border
                border.width: 1

                Column {
                    id: destCol
                    anchors.fill: parent
                    anchors.margins: Theme.dp(10)
                    spacing: Theme.dp(6)

                    Text {
                        text: qsTr("Output POD Filename")
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        font.weight: Font.DemiBold
                        color: Theme.textMuted
                    }

                    TextField {
                        id: destField
                        width: parent.width
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textPrimary
                        selectByMouse: true
                        background: Rectangle {
                            radius: Theme.radiusSm
                            color: Theme.surface2
                            border.color: Theme.border
                            border.width: 1
                        }
                    }
                }
            }

            // Geometry Scale Section
            Rectangle {
                width: parent.width
                height: scaleCol.implicitHeight + Theme.dp(20)
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.border
                border.width: 1

                Column {
                    id: scaleCol
                    anchors.fill: parent
                    anchors.margins: Theme.dp(10)
                    spacing: Theme.dp(8)

                    Text {
                        text: qsTr("Geometry Scale")
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        font.weight: Font.DemiBold
                        color: Theme.textMuted
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        TextField {
                            id: scaleField
                            width: Theme.dp(100)
                            text: "1.0"
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            color: Theme.textPrimary
                            background: Rectangle {
                                radius: Theme.radiusSm
                                color: Theme.surface2
                                border.color: Theme.border
                                border.width: 1
                            }
                        }

                        // Preset chips
                        ScrollView {
                            width: scaleCol.width - Theme.dp(110)
                            height: Theme.dp(36)
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            ScrollBar.vertical.policy: ScrollBar.AlwaysOff

                            Row {
                                spacing: Theme.dp(4)

                                Repeater {
                                    model: [
                                        { label: "1.0x", val: "1.0" },
                                        { label: "m→ft", val: "3.2808" },
                                        { label: "0.01x", val: "0.01" }
                                    ]

                                    delegate: Button {
                                        text: modelData.label
                                        flat: true
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        onClicked: scaleField.text = modelData.val
                                    }
                                }
                            }
                        }
                    }

                    // Auto-Fit Reference Height Buttons
                    Row {
                        width: parent.width
                        spacing: Theme.dp(4)
                        visible: root.modelInfo && root.modelInfo.height > 0.001

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Auto-fit:")
                            font.pixelSize: Theme.dp(10)
                            color: Theme.textMuted
                        }

                        Repeater {
                            model: [
                                { label: qsTr("Hero (70u)"), h: 70.0 },
                                { label: qsTr("Prop (100u)"), h: 100.0 },
                                { label: qsTr("Decor (250u)"), h: 250.0 }
                            ]

                            delegate: Button {
                                text: modelData.label
                                flat: true
                                font.pixelSize: Theme.dp(10)
                                onClicked: {
                                    if (root.modelInfo && root.modelInfo.height > 0.001) {
                                        var s = modelData.h / root.modelInfo.height
                                        scaleField.text = s.toFixed(4)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Texture & Compression Options
            Rectangle {
                width: parent.width
                height: texCol.implicitHeight + Theme.dp(20)
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.border
                border.width: 1

                Column {
                    id: texCol
                    anchors.fill: parent
                    anchors.margins: Theme.dp(10)
                    spacing: Theme.dp(4)

                    CheckBox {
                        id: flipUvCheck
                        checked: true
                        text: qsTr("Flip UVs vertically (Swordigo bottom-up UVs)")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                    }

                    CheckBox {
                        id: convertTexCheck
                        checked: true
                        text: qsTr("Compress textures to game .pvr (ETC1)")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                    }

                    CheckBox {
                        id: filterNormalsCheck
                        checked: true
                        text: qsTr("Filter non-diffuse maps (normals, specular)")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                    }

                    CheckBox {
                        id: smartNamingCheck
                        checked: true
                        text: qsTr("Smart texture naming (avoid collisions)")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                    }

                    CheckBox {
                        id: rigidSkinCheck
                        checked: true
                        text: qsTr("Dominant-bone rigid skin bake (game engine parity)")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                    }
                }
            }

            // Conversion Status / Error / Success
            Rectangle {
                width: parent.width
                height: statusCol.implicitHeight + Theme.dp(16)
                radius: Theme.radiusCard
                visible: root.isConverting || root.conversionError.length > 0 || root.convertedPodPath.length > 0
                color: root.convertedPodPath.length > 0 ? Theme.alpha(Theme.colorSuccess, 0.15)
                     : root.conversionError.length > 0 ? Theme.alpha(Theme.colorDanger, 0.15)
                     : Theme.surface2
                border.color: root.convertedPodPath.length > 0 ? Theme.colorSuccess
                            : root.conversionError.length > 0 ? Theme.colorDanger
                            : Theme.border
                border.width: 1

                Column {
                    id: statusCol
                    anchors.fill: parent
                    anchors.margins: Theme.dp(10)
                    spacing: Theme.dp(6)

                    Row {
                        spacing: Theme.dp(6)
                        visible: root.isConverting
                        BusyIndicator { running: root.isConverting; width: Theme.dp(20); height: Theme.dp(20) }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Converting geometry & compressing ETC1 textures...")
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            color: Theme.accentInk
                        }
                    }

                    Text {
                        width: parent.width
                        visible: root.conversionError.length > 0
                        text: root.conversionError
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.colorError
                        wrapMode: Text.WordWrap
                    }

                    Column {
                        width: parent.width
                        visible: root.convertedPodPath.length > 0
                        spacing: Theme.dp(6)

                        Text {
                            text: qsTr("Successfully converted to Game POD!")
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            font.weight: Font.Bold
                            color: Theme.colorSuccess
                        }

                        Button {
                            text: qsTr("Open in 3D Viewport")
                            highlighted: true
                            onClicked: {
                                var p = root.convertedPodPath
                                root.close()
                                root.openConvertedModel(p)
                            }
                        }
                    }
                }
            }

            // Action Buttons
            Row {
                anchors.right: parent.right
                spacing: Theme.dp(Theme.spacingSm)

                Button {
                    text: qsTr("Close")
                    flat: true
                    onClicked: root.close()
                }

                Button {
                    text: qsTr("Convert to POD")
                    highlighted: true
                    enabled: !root.isConverting && root.sourcePath.length > 0
                    onClicked: {
                        var parts = root.sourcePath.split("/")
                        parts.pop()
                        var destDir = parts.join("/")
                        var dest = destDir + "/" + destField.text.trim()
                        var scale = parseFloat(scaleField.text) || 1.0

                        var options = {
                            "sourcePath": root.sourcePath,
                            "destPath": dest,
                            "scale": scale,
                            "flipUv": flipUvCheck.checked,
                            "convertTextures": convertTexCheck.checked,
                            "filterNormals": filterNormalsCheck.checked,
                            "smartNaming": smartNamingCheck.checked,
                            "rigidSkin": rigidSkinCheck.checked,
                            "pvrResolution": 0,
                            "animSource": 0,
                            "animFps": 24.0,
                            "overwrite": true
                        }
                        toolsBridge.convertModelAdvanced(options)
                    }
                }
            }
        }
    }
}
