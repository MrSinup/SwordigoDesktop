import QtQuick
import QtQuick.Controls
import ".."

// ============================================================================
// NumericKeypad.qml — a real touch number pad for transform fields.
//
// WHY THIS EXISTS
//   In landscape the soft keyboard covers most of the 3D viewport, and typing
//   "-12.75" on it is a five-tap precision exercise with a cursor that lands
//   wherever the tap guessed. Every numeric field in the inspector therefore
//   stays `readOnly` and a tap routes into this pad instead, where each digit
//   is a 48dp target and the sign/point/backspace are explicit.
//
// It edits whatever TextInput is handed to `target`, so there is no second copy
// of the value anywhere — the field stays the single source of truth and
// `editingFinished` tells the owner to commit it to the viewport.
//
// It is deliberately NOT a whole keypad for text: names and templates keep the
// system keyboard, because they need letters.
// ============================================================================
Item {
    id: root

    /// TextInput (or TextField) being edited. May be null.
    property var target: null
    /// Caption above the pad, e.g. "Position X".
    property string label: ""
    /// Suffix shown next to the live value, e.g. "deg", "u".
    property string unit: ""

    signal editingFinished()
    signal cancelled()

    implicitHeight: grid.implicitHeight + header.height + Theme.dp(Theme.spacingSm)
    implicitWidth: Math.max(grid.implicitWidth, header.implicitWidth)

    readonly property real keySize: Theme.dp(42)
    readonly property real keyGap: Theme.dp(5)

    // ── Editing primitives ─────────────────────────────────────────────────
    function insert(text) {
        if (!root.target) return
        var at = root.target.cursorPosition >= 0 ? root.target.cursorPosition
                                                 : root.target.text.length
        root.target.text = root.target.text.slice(0, at) + text + root.target.text.slice(at)
        root.target.cursorPosition = at + text.length
    }

    function backspace() {
        if (!root.target) return
        var at = root.target.cursorPosition
        if (at <= 0) return
        root.target.text = root.target.text.slice(0, at - 1) + root.target.text.slice(at)
        root.target.cursorPosition = at - 1
    }

    /// Flips the leading sign. Works on a partly-typed value too, so "-" is a
    /// toggle rather than a character that can be inserted twice.
    function toggleSign() {
        if (!root.target) return
        var t = root.target.text
        root.target.text = (t.charAt(0) === "-") ? t.slice(1) : ("-" + t)
        root.target.cursorPosition = root.target.text.length
    }

    function clearAll() {
        if (!root.target) return
        root.target.text = ""
        root.target.cursorPosition = 0
    }

    function currentText() {
        return root.target ? root.target.text : ""
    }

    // ── Key table: 16 keys on a 4x4 grid. ─────────────────────────────────
    // Four rows, not five: in landscape every design unit of height taken by
    // the pad is a unit the 3D scene does not get, and 4x4 still carries every
    // digit, the point, the sign, a double-zero and a step of 0.1.
    readonly property var keys: [
        "7", "8", "9", "\u232B",
        "4", "5", "6", "C",
        "1", "2", "3", "\u00B1",
        "0", ".", "00", "0.1"
    ]

    function keyTone(k) {
        if (k === "\u232B" || k === "C") return Theme.colorErrorBright
        if (k === "\u00B1" || k === "0.1") return Theme.accentInk
        return Theme.textPrimary
    }

    function keyPlate(k) {
        if (k === "\u232B" || k === "C") return Theme.alpha(Theme.colorError, 0.24)
        if (k === "\u00B1" || k === "0.1") return Theme.alpha(Theme.accentInk, 0.12)
        return Theme.surface2
    }

    function pressKey(k) {
        switch (k) {
        case "\u232B": root.backspace(); break
        case "C":      root.clearAll(); break
        case "\u00B1": root.toggleSign(); break
        case "0.1":    root.insert("0.1"); break
        default:       root.insert(k); break
        }
    }

    // ── Live value readout ─────────────────────────────────────────────────
    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.dp(44)
        radius: Theme.radiusSm
        color: Theme.surfaceSunken
        border.color: Theme.borderStrong
        border.width: 1

        Column {
            anchors.left: parent.left
            anchors.leftMargin: Theme.dp(Theme.spacingMd)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            Text {
                text: root.label.length > 0 ? root.label.toUpperCase() : qsTr("VALUE")
                font.pixelSize: Theme.dp(9)
                font.weight: Font.Bold
                font.letterSpacing: 1.0
                color: Theme.textMuted
            }

            Row {
                spacing: Theme.dp(4)

                Text {
                    id: valueText
                    text: root.currentText().length > 0 ? root.currentText() : "0"
                    font.pixelSize: Theme.dp(Theme.fontXl)
                    font.family: Theme.monoFamily
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                }

                Text {
                    anchors.baseline: valueText.baseline
                    visible: root.unit.length > 0
                    text: root.unit
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textMuted
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.dp(Theme.spacingSm)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.dp(2)

            IconButton {
                iconName: "undo"
                buttonSize: Theme.dp(34)
                iconSize: Theme.dp(16)
                onClicked: root.clearAll()
            }

            IconButton {
                iconName: "check"
                variant: "primary"
                buttonSize: Theme.dp(34)
                iconSize: Theme.dp(16)
                onClicked: root.editingFinished()
            }
        }
    }

    Grid {
        id: grid
        anchors.top: header.bottom
        anchors.topMargin: Theme.dp(Theme.spacingSm)
        anchors.horizontalCenter: parent.horizontalCenter
        columns: 4
        columnSpacing: root.keyGap
        rowSpacing: root.keyGap

        Repeater {
            model: root.keys

            delegate: Item {
                width: root.keySize
                height: root.keySize
                visible: modelData !== null

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSm
                    color: keyArea.pressed ? Theme.alpha(root.keyTone(modelData), 0.30)
                                           : root.keyPlate(modelData)
                    border.color: keyArea.pressed ? Theme.alpha(root.keyTone(modelData), 0.55)
                                                  : Theme.borderSubtle
                    border.width: 1
                }

                Icon {
                    anchors.centerIn: parent
                    visible: modelData === "\u232B"
                    name: "arrow-left"
                    size: Theme.dp(18)
                    color: root.keyTone(modelData)
                }

                Text {
                    anchors.centerIn: parent
                    visible: modelData !== "\u232B"
                    text: modelData === null ? "" : modelData
                    font.pixelSize: Theme.dp(modelData === "00" || modelData === "0.1"
                                             ? Theme.fontMd : Theme.fontXl)
                    font.family: Theme.monoFamily
                    font.weight: Font.DemiBold
                    color: root.keyTone(modelData)
                }

                MouseArea {
                    id: keyArea
                    anchors.fill: parent
                    enabled: modelData !== null
                    onClicked: root.pressKey(modelData)
                }
            }
        }
    }
}
