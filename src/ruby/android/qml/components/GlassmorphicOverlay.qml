import QtQuick
import QtQuick.Controls
import ".."

// Floating translucent surface used for HUD pills and the gizmo mode selector.
// Kept as a plain translucent panel (no QtQuick.Effects dependency) so it costs
// nothing to composite over the GL viewport on mobile GPUs.
Rectangle {
    id: root

    radius: Theme.radiusCard
    color: Theme.surfaceGlass
    border.color: Theme.border
    border.width: 1

    default property alias contentData: innerContainer.data

    Item {
        id: innerContainer
        anchors.fill: parent
        anchors.margins: Theme.dp(6)
    }
}
