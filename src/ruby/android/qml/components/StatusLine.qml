import QtQuick
import QtQuick.Controls
import ".."

// Inline result / progress line. Hidden until it has something to say.
Text {
    width: parent ? parent.width : implicitWidth
    visible: text.length > 0
    font.pixelSize: Theme.dp(Theme.fontSm)
    color: Theme.accentInk
    wrapMode: Text.WordWrap
}
