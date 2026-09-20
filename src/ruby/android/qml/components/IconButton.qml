import QtQuick
import QtQuick.Controls
import ".."

// The single tappable icon control for the whole app. Replaces the ad-hoc
// Rectangle+Text("⋮"/"✕"/"←") clusters that used to be copied into every view.
//
// Variants: "plain" | "soft" | "primary" | "danger"
Item {
    id: root

    property string iconName: ""
    property string variant: "plain"
    property real buttonSize: Theme.dp(Theme.iconButtonSize)
    property real iconSize: Theme.dp(Theme.iconMd)
    property real iconWeight: Icons.stroke
    /// Optional override; defaults to a variant-appropriate tint.
    property color tint: toneOf(variant)
    property color plate: plateOf(variant)
    property color plateEdge: edgeOf(variant)
    /// Show a rounded square instead of a circle (toolbar buttons).
    property bool square: false
    property string tooltip: ""

    signal clicked()

    function toneOf(v) {
        switch (v) {
        case "primary": return Theme.onAccent
        case "danger":  return Theme.colorErrorBright
        case "soft":    return Theme.accentInk
        default:        return Theme.textSecondary
        }
    }

    function plateOf(v) {
        switch (v) {
        case "primary": return Theme.accentStart
        case "danger":  return Theme.alpha(Theme.colorError, 0.30)
        case "soft":    return Theme.alpha(Theme.accentInk, 0.14)
        default:        return "transparent"
        }
    }

    function edgeOf(v) {
        switch (v) {
        case "primary": return Theme.accentEnd
        case "danger":  return Theme.alpha(Theme.colorErrorBright, 0.45)
        case "soft":    return Theme.alpha(Theme.accentInk, 0.28)
        default:        return "transparent"
        }
    }

    implicitWidth: buttonSize
    implicitHeight: buttonSize
    opacity: enabled ? 1.0 : 0.35

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(root.buttonSize, root.width > 0 ? root.width : root.buttonSize)
        height: width
        radius: root.square ? Theme.radiusMd : width / 2
        color: hit.pressed ? Theme.alpha(root.tint, 0.18) : root.plate
        border.color: root.plateEdge
        border.width: root.plateEdge.a > 0 ? 1 : 0

        Behavior on color { ColorAnimation { duration: Theme.durFast } }

        Icon {
            anchors.centerIn: parent
            name: root.iconName
            size: root.iconSize
            weight: root.iconWeight
            color: root.tint
        }
    }

    MouseArea {
        id: hit
        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
