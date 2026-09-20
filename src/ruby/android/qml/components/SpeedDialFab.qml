import QtQuick
import QtQuick.Controls
import ".."

// Create-menu speed dial. Contract preserved: expanded,
// newSceneRequested / newScriptRequested / newFolderRequested /
// importAssetRequested.
Item {
    id: root

    width: Theme.dp(56)
    height: Theme.dp(56)
    property bool expanded: false

    signal newFileWizardRequested()
    signal newSceneRequested()
    signal newScriptRequested()
    signal newFolderRequested()
    signal importAssetRequested()

    // Outside-tap catcher
    Rectangle {
        id: scrim
        visible: root.expanded
        opacity: root.expanded ? 0.55 : 0.0
        color: "#000000"
        anchors.fill: parent
        anchors.margins: -2000

        Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

        MouseArea {
            anchors.fill: parent
            onClicked: root.expanded = false
        }
    }

    Column {
        anchors.bottom: mainFab.top
        anchors.bottomMargin: Theme.dp(14)
        anchors.horizontalCenter: mainFab.horizontalCenter
        spacing: Theme.dp(Theme.spacingMd)
        visible: root.expanded
        opacity: root.expanded ? 1.0 : 0.0

        Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

        Repeater {
            model: [
                { label: qsTr("New File Wizard..."), icon: "sparkles",    accent: Theme.accentStart,  action: "wizard" },
                { label: qsTr("New Scene (.scene)"), icon: "map",         accent: Theme.colorScene,   action: "scene" },
                { label: qsTr("New Script (.lua)"),  icon: "braces",      accent: Theme.colorCode,    action: "script" },
                { label: qsTr("New Folder"),         icon: "folder-plus", accent: Theme.colorFolder,  action: "folder" },
                { label: qsTr("Import Asset"),       icon: "download",    accent: Theme.colorTexture, action: "import" }
            ]

            Row {
                anchors.right: parent.right
                spacing: Theme.dp(Theme.spacingSm)

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    height: Theme.dp(30)
                    width: actionLabel.implicitWidth + Theme.dp(20)
                    radius: Theme.radiusPill
                    color: Theme.surface2
                    border.color: Theme.border
                    border.width: 1

                    Text {
                        id: actionLabel
                        anchors.centerIn: parent
                        text: modelData.label
                        color: modelData.accent
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        font.weight: Font.DemiBold
                    }
                }

                Rectangle {
                    width: Theme.dp(46)
                    height: Theme.dp(46)
                    radius: width / 2
                    color: miniFabArea.pressed ? Theme.alpha(modelData.accent, 0.85)
                                               : Theme.alpha(modelData.accent, 0.18)
                    border.color: Theme.alpha(modelData.accent, 0.45)
                    border.width: 1

                    Icon {
                        anchors.centerIn: parent
                        name: modelData.icon
                        size: Theme.dp(Theme.iconMd)
                        color: modelData.accent
                    }

                    MouseArea {
                        id: miniFabArea
                        anchors.fill: parent
                        onClicked: {
                            root.expanded = false
                            switch (modelData.action) {
                            case "wizard": root.newFileWizardRequested(); break
                            case "scene":  root.newSceneRequested(); break
                            case "script": root.newScriptRequested(); break
                            case "folder": root.newFolderRequested(); break
                            case "import": root.importAssetRequested(); break
                            }
                        }
                    }
                }
            }
        }
    }

    // Main FAB
    Rectangle {
        id: mainFab
        anchors.fill: parent
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.accentEnd }
            GradientStop { position: 1.0; color: Theme.accentStart }
        }
        border.color: Theme.alpha(Theme.accentInk, 0.45)
        border.width: 1

        Icon {
            anchors.centerIn: parent
            name: root.expanded ? "close" : "plus"
            size: Theme.dp(root.expanded ? 22 : 24)
            weight: 2.2
            color: Theme.onAccent

            Behavior on size { NumberAnimation { duration: Theme.durMed; easing.type: Easing.OutBack } }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.expanded = !root.expanded
        }
    }
}
