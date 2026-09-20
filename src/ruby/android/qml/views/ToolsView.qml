import QtQuick
import QtQuick.Controls
import ".."
import "../components"

Item {
    id: root

    property string activeTool: ""

    signal fileSelectRequested(string targetField)
    signal openMeshStudioRequested()

    readonly property var tools: [
        { id: "converter",   label: qsTr("Model POD"),    icon: "cube",     accent: Theme.colorModel },
        { id: "ground_mesh", label: qsTr("Ground Mesh"),  icon: "mountain", accent: Theme.colorSuccessBright },
        { id: "batch",       label: qsTr("Batch"),        icon: "bolt",     accent: Theme.colorArchive }
    ]

    Column {
        anchors.fill: parent
        anchors.leftMargin: Theme.dp(Theme.paddingScreen)
        anchors.rightMargin: Theme.dp(Theme.paddingScreen)
        anchors.topMargin: Theme.dp(Theme.spacingLg)
        spacing: Theme.dp(Theme.spacingMd)

        Text {
            text: qsTr("Production Tools")
            font.pixelSize: Theme.dp(Theme.fontTitle)
            font.weight: Font.Bold
            color: Theme.textPrimary
        }

        // ── Tool selector ──────────────────────────────────────────────────
        Row {
            width: parent.width
            spacing: Theme.dp(Theme.spacingSm)

            Repeater {
                model: root.tools

                Chip {
                    width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.label
                    iconName: modelData.icon
                    accent: modelData.accent
                    selected: root.activeTool === modelData.id
                              || (root.activeTool === "" && index === 0)
                    onClicked: root.activeTool = modelData.id
                }
            }
        }

        // ── Tool body ──────────────────────────────────────────────────────
        Flickable {
            width: parent.width
            height: parent.height - Theme.dp(96)
            contentHeight: body.implicitHeight + Theme.dp(Theme.spacingXl)
            clip: true

            Column {
                id: body
                width: parent.width
                spacing: Theme.dp(Theme.spacingMd)

                // 1 ── Model → POD
                SectionCard {
                    width: parent.width
                    visible: root.activeTool === "converter" || root.activeTool === ""
                    label: qsTr("Convert 3D Model to Game POD")
                    accentIcon: "cube"
                    accent: Theme.colorModel

                    Text {
                        width: parent.width
                        text: qsTr("Converts GLTF, GLB, FBX or OBJ into a game-ready .POD optimised for Swordigo.")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    FieldLabel { text: qsTr("Source 3D model") }
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        ThemedField {
                            id: sourceModelPath
                            width: parent.width - Theme.dp(Theme.iconButtonSize) - Theme.dp(Theme.spacingSm)
                            placeholderText: qsTr("Source GLB, OBJ, FBX path")
                            onTextChanged: {
                                var s = text.trim()
                                if (s.length > 0 && outputPodPath.text.length === 0) {
                                    var dot = s.lastIndexOf(".")
                                    if (dot > 0) outputPodPath.text = s.substring(0, dot) + ".pod"
                                    else outputPodPath.text = s + ".pod"
                                }
                            }
                        }

                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "folder-open"
                            variant: "soft"
                            buttonSize: Theme.dp(Theme.iconButtonSize)
                            onClicked: {
                                if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                    sourceModelPath.text = rubyFileModel.currentPath + "/model.glb"
                                }
                            }
                        }
                    }

                    FieldLabel { text: qsTr("Output POD path") }
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        ThemedField {
                            id: outputPodPath
                            width: parent.width - Theme.dp(Theme.iconButtonSize) - Theme.dp(Theme.spacingSm)
                            placeholderText: qsTr("Target .pod destination path")
                        }

                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "drive"
                            variant: "soft"
                            buttonSize: Theme.dp(Theme.iconButtonSize)
                            onClicked: {
                                if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                    outputPodPath.text = rubyFileModel.currentPath + "/model.pod"
                                }
                            }
                        }
                    }

                    FieldLabel { text: qsTr("Swordigo scale preset") }
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)
                        Repeater {
                            model: [
                                { label: qsTr("Hero 1.8m"), val: 1.0 },
                                { label: qsTr("Prop 1.0m"), val: 0.55 },
                                { label: qsTr("Decor 0.5m"), val: 0.28 },
                                { label: qsTr("Large 4.0m"), val: 2.22 }
                            ]
                            delegate: Chip {
                                text: modelData.label
                                accent: Theme.colorModel
                                onClicked: scaleField.text = modelData.val.toString()
                            }
                        }
                    }

                    ThemedField {
                        id: scaleField
                        width: Theme.dp(120)
                        text: "1.0"
                        placeholderText: qsTr("scale")
                    }

                    SettingRow {
                        width: parent.width
                        title: qsTr("Flip vertical UV coordinates")
                        description: qsTr("OpenGL standard — leave on unless the texture looks mirrored")
                        hasSwitch: true
                        checked: true
                    }

                    PrimaryButton {
                        width: parent.width
                        text: qsTr("Run Conversion to POD")
                        iconName: "play"
                        onClicked: toolsBridge.convertModel(
                                       sourceModelPath.text,
                                       outputPodPath.text,
                                       parseFloat(scaleField.text) || 1.0,
                                       true)
                    }

                    StatusLine { text: toolsBridge.lastStatusMessage }
                }

                // 2 ── Ground mesh
                SectionCard {
                    width: parent.width
                    visible: root.activeTool === "ground_mesh"
                    label: qsTr("Ground Mesh & Collision Zones")
                    accentIcon: "mountain"
                    accent: Theme.colorSuccessBright

                    Text {
                        width: parent.width
                        text: qsTr("Build collision terrain and walking paths from a scene's model vertices.")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    FieldLabel { text: qsTr("Scene / RBM file") }
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        ThemedField {
                            id: rbmFilePath
                            width: parent.width - Theme.dp(Theme.iconButtonSize) - Theme.dp(Theme.spacingSm)
                            placeholderText: qsTr("Path to .scene or .rbm file")
                        }

                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "folder-open"
                            variant: "soft"
                            buttonSize: Theme.dp(Theme.iconButtonSize)
                            onClicked: {
                                if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                    rbmFilePath.text = rubyFileModel.currentPath
                                }
                            }
                        }
                    }

                    PrimaryButton {
                        width: parent.width
                        text: qsTr("Generate Ground Collision")
                        iconName: "mountain"
                        onClicked: toolsBridge.generateGroundMesh(rbmFilePath.text)
                    }

                    PrimaryButton {
                        width: parent.width
                        text: qsTr("Open Ground Mesh Sheet Studio (Model B)")
                        iconName: "edit"
                        onClicked: root.openMeshStudioRequested()
                    }

                    StatusLine { text: toolsBridge.lastStatusMessage }
                }

                // 3 ── Batch textures
                SectionCard {
                    width: parent.width
                    visible: root.activeTool === "batch"
                    label: qsTr("Batch Texture Converter")
                    accentIcon: "bolt"
                    accent: Theme.colorArchive

                    Text {
                        width: parent.width
                        text: qsTr("Converts every texture in a folder between .PVR and .PNG.")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    FieldLabel { text: qsTr("Folder") }
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        ThemedField {
                            id: batchFolderPath
                            width: parent.width - Theme.dp(Theme.iconButtonSize) - Theme.dp(Theme.spacingSm)
                            placeholderText: qsTr("Folder containing textures")
                        }

                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "folder-open"
                            variant: "soft"
                            buttonSize: Theme.dp(Theme.iconButtonSize)
                            onClicked: {
                                if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                    batchFolderPath.text = rubyFileModel.currentPath
                                }
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        PrimaryButton {
                            width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                            text: qsTr("PVR → PNG")
                            iconName: "image"
                            onClicked: toolsBridge.batchConvertTextures(batchFolderPath.text, "png")
                        }

                        PrimaryButton {
                            width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                            text: qsTr("PNG → PVR")
                            iconName: "archive"
                            onClicked: toolsBridge.batchConvertTextures(batchFolderPath.text, "pvr")
                        }
                    }

                    StatusLine { text: toolsBridge.lastStatusMessage }
                }
            }
        }
    }
}
