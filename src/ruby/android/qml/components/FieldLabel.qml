import QtQuick
import QtQuick.Controls
import ".."

// Uppercase micro-label above a form field.
Text {
    width: parent ? parent.width : implicitWidth
    font.pixelSize: Theme.dp(Theme.fontXs)
    font.weight: Font.Bold
    font.letterSpacing: 1.0
    color: Theme.textMuted
    topPadding: Theme.dp(2)
}
