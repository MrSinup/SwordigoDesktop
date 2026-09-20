import QtQuick
import QtQuick.Controls
import ".."

// Soft-keyboard helper strip for the code editor. The literal punctuation keys
// stay as text (they ARE the subject matter and are pure ASCII, so they always
// render); only the tab/undo/redo controls use vector icons.
Item {
    id: root

    height: Theme.dp(50)
    anchors.left: parent.left
    anchors.right: parent.right

    signal insertText(string text)
    signal tabRequested()
    signal untabRequested()
    signal undoRequested()
    signal redoRequested()

    Rectangle {
        anchors.fill: parent
        color: Theme.surface1

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.borderSubtle
        }

        ListView {
            id: listView
            anchors.fill: parent
            anchors.leftMargin: Theme.dp(8)
            anchors.rightMargin: Theme.dp(8)
            orientation: ListView.Horizontal
            spacing: Theme.dp(6)
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick

            model: [
                { key: "tab",    icon: "indent",  action: "tab"   },
                { key: "untab",  icon: "outdent", action: "untab" },
                { text: "{" }, { text: "}" },
                { text: "(" }, { text: ")" },
                { text: "[" }, { text: "]" },
                { text: "$" }, { text: "\"" },
                { text: "=" }, { text: ":" },
                { text: "." }, { text: "," },
                { text: "_" }, { text: ";" },
                { key: "undo",   icon: "undo", action: "undo" },
                { key: "redo",   icon: "redo", action: "redo" }
            ]

            delegate: Rectangle {
                width: modelData.icon ? Theme.dp(46) : Theme.dp(38)
                height: Theme.dp(36)
                anchors.verticalCenter: parent.verticalCenter
                radius: Theme.radiusSm
                color: keyArea.pressed ? Theme.accentStart : Theme.surface2
                border.color: keyArea.pressed ? Theme.borderFocus : Theme.borderSubtle
                border.width: 1

                Behavior on color { ColorAnimation { duration: Theme.durFast } }

                Icon {
                    anchors.centerIn: parent
                    visible: !!modelData.icon
                    name: modelData.icon || ""
                    size: Theme.dp(19)
                    color: keyArea.pressed ? Theme.onAccent : Theme.accentInk
                }

                Text {
                    anchors.centerIn: parent
                    visible: !modelData.icon
                    text: modelData.text || ""
                    font.pixelSize: Theme.dp(15)
                    font.family: Theme.monoFamily
                    font.weight: Font.Medium
                    color: keyArea.pressed ? Theme.onAccent : Theme.textPrimary
                }

                MouseArea {
                    id: keyArea
                    anchors.fill: parent
                    onClicked: {
                        switch (modelData.action) {
                        case "tab":   root.tabRequested(); break
                        case "untab": root.untabRequested(); break
                        case "undo":  root.undoRequested(); break
                        case "redo":  root.redoRequested(); break
                        default:      if (modelData.text) root.insertText(modelData.text)
                        }
                    }
                }
            }
        }
    }
}
