import QtQuick
import QtQuick.Controls
import ".."

// Themed single-field input dialog (rename / new file / new folder).
// Emits accepted(string) with the trimmed value; empty input is rejected
// in-place rather than silently accepted, matching the legacy QInputDialog
// guards (`if (ok && !name.trimmed().isEmpty())`).
Popup {
    id: root

    property string title: qsTr("Input")
    property string label: ""
    property string placeholder: ""
    property string initialText: ""
    property string acceptText: qsTr("OK")
    /// Shown when the user confirms with an empty field.
    property string emptyWarning: qsTr("This field cannot be empty.")

    signal accepted(string value)

    /// Flips on when the user confirms with an empty field.
    property bool showWarning: false

    function openWith(text) {
        root.initialText = text
        root.open()
    }

    function tryAccept() {
        var value = field.text.trim()
        if (value.length === 0) {
            root.showWarning = true
            field.forceActiveFocus()
            return
        }
        root.showWarning = false
        root.close()
        root.accepted(value)
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - Theme.dp(48) : Theme.dp(320), Theme.dp(360))
    height: content.implicitHeight + Theme.dp(28)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0

    onOpened: {
        root.showWarning = false
        field.text = root.initialText
        field.forceActiveFocus()
        field.selectAll()
    }

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

        Text {
            width: parent.width
            text: root.title
            font.pixelSize: Theme.dp(Theme.fontXl)
            font.weight: Font.DemiBold
            color: Theme.textPrimary
        }

        Column {
            width: parent.width
            spacing: Theme.dp(Theme.spacingXs)

            Text {
                visible: root.label.length > 0
                text: root.label
                font.pixelSize: Theme.dp(Theme.fontXs)
                font.weight: Font.Bold
                font.letterSpacing: 1.0
                color: Theme.textMuted
            }

            Rectangle {
                width: parent.width
                height: Theme.dp(42)
                radius: Theme.radiusSm
                color: Theme.surface2
                border.color: field.activeFocus ? Theme.borderFocus : Theme.border
                border.width: 1

                TextInput {
                    id: field
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(Theme.spacingMd)
                    anchors.rightMargin: Theme.dp(Theme.spacingMd)
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.textPrimary
                    font.pixelSize: Theme.dp(Theme.fontMd)
                    selectByMouse: true
                    clip: true
                    onTextChanged: root.showWarning = false
                    onAccepted: root.tryAccept()

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.placeholder
                        color: Theme.textMuted
                        font.pixelSize: Theme.dp(Theme.fontMd)
                        visible: !field.text && !field.activeFocus && root.placeholder.length > 0
                    }
                }
            }

            Text {
                width: parent.width
                visible: root.showWarning
                text: root.emptyWarning
                font.pixelSize: Theme.dp(Theme.fontXs)
                color: Theme.colorErrorBright
                wrapMode: Text.WordWrap
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
                color: Theme.accentStart
                border.color: Theme.accentEnd
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: root.acceptText
                    font.pixelSize: Theme.dp(Theme.fontMd)
                    font.weight: Font.DemiBold
                    color: Theme.onAccent
                }

                MouseArea {
                    id: acceptArea
                    anchors.fill: parent
                    onClicked: root.tryAccept()
                }
            }

            Rectangle {
                width: Theme.dp(96)
                height: Theme.dp(40)
                radius: Theme.radiusSm
                color: "transparent"
                border.color: Theme.borderStrong
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: qsTr("Cancel")
                    font.pixelSize: Theme.dp(Theme.fontMd)
                    color: Theme.textSecondary
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.close()
                }
            }
        }
    }
}
