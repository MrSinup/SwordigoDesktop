import QtQuick
import QtQuick.Controls
import ".."

// Text input with the app's field styling.
TextField {
    width: parent ? parent.width : implicitWidth
    height: Theme.dp(42)
    color: Theme.textPrimary
    font.pixelSize: Theme.dp(Theme.fontMd)
    placeholderTextColor: Theme.textMuted
    selectByMouse: true

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.surface2
        border.color: parent && parent.activeFocus ? Theme.borderFocus : Theme.borderSubtle
        border.width: 1
    }
}
