import QtQuick
import QtQuick.Controls
import ".."

// ============================================================================
// KeypadField.qml — a numeric field that is edited by the NumericKeypad, not by
// the system soft keyboard.
//
// The field stays the single source of truth for the value (so every existing
// `parseFloat(field.text)` commit path keeps working unchanged); it is simply
// `readOnly` and covered by a MouseArea, so a tap routes the field into the
// shared keypad instead of raising the IME. That is what makes precise entry
// possible in landscape, where the soft keyboard would cover the viewport.
//
// The focused field is marked with an accent ring so it is unambiguous which
// value the keypad is editing when nine of them are on screen.
// ============================================================================
ThemedField {
    id: root

    /// True while the keypad is editing THIS field.
    property bool routed: false
    /// Shown in the keypad header, e.g. "Position X".
    property string keyLabel: ""

    /// A tap anywhere on the field asks the owner to route the keypad here.
    signal tapRequested()

    readOnly: true
    // A read-only field should never raise the IME even if a tap slips through.
    inputMethodHints: Qt.ImhNone
    onActiveFocusChanged: if (activeFocus) focus = false

    background: Rectangle {
        radius: Theme.radiusSm
        color: root.routed ? Theme.alpha(Theme.accentInk, 0.10) : Theme.surface2
        border.color: root.routed ? Theme.borderFocus : Theme.borderSubtle
        border.width: root.routed ? 2 : 1

        Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
    }

    MouseArea {
        anchors.fill: parent
        // Above the control's own contentItem, so the press can never reach the
        // text input and raise the IME on a device.
        z: 1
        acceptedButtons: Qt.LeftButton
        onClicked: root.tapRequested()
    }
}
