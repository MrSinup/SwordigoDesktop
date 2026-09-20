import QtQuick
import QtQuick.Controls
import ".."

// ============================================================================
// NavPad.qml — Vibrant tactile gamepad controller for viewport navigation.
//
// Combines authentic Swordigo/Ruby GG vibrant glowing style (luminous sapphire
// for PAN, luminous gold/amber for FLY) with 60 FPS GPU declarative rendering.
// ============================================================================
Item {
    id: root

    signal panRequested(real dx, real dy)
    signal dollyRequested(real forward, real strafe)
    signal modeChanged(int newMode)

    enum NavMode { Pan, Dolly }

    property int navMode: NavPad.Pan
    property bool allowModeToggle: false
    property bool showModeHint: false

    readonly property bool isPan: navMode === NavPad.Pan
    readonly property real outerRadius: 42
    readonly property real deadZone: 8
    readonly property real knobRadius: 20

    width: Theme.dp(84)
    height: Theme.dp(84)

    readonly property real unit: width / 84.0
    readonly property point centre: Qt.point(width / 2, height / 2)

    property bool pressed: false
    property point knobPos: centre
    property real vecX: 0.0
    property real vecY: 0.0

    opacity: pressed ? 1.0 : 0.85
    Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

    function updateFromPos(px, py) {
        var dx = px - centre.x
        var dy = py - centre.y
        var dist = Math.sqrt(dx * dx + dy * dy)

        if (dist <= deadZone * unit) {
            knobPos = centre
            vecX = 0.0
            vecY = 0.0
            return
        }

        var maxDist = (outerRadius - knobRadius * 0.5) * unit
        var clamped = Math.min(dist, maxDist)
        var dirX = dx / dist
        var dirY = dy / dist

        knobPos = Qt.point(centre.x + dirX * clamped, centre.y + dirY * clamped)

        var dzScaled = deadZone * unit
        var intensity = (clamped - dzScaled) / (maxDist - dzScaled)
        vecX = dirX * intensity
        vecY = dirY * intensity
    }

    function resetVector() {
        knobPos = centre
        vecX = 0.0
        vecY = 0.0
    }

    function beginPress(px, py) {
        pressed = true
        updateFromPos(px, py)
        tickTimer.start()
    }

    function movePress(px, py) {
        if (pressed) updateFromPos(px, py)
    }

    function endPress(px, py) {
        var dx = px - centre.x
        var dy = py - centre.y
        var distFromCentre = Math.sqrt(dx * dx + dy * dy)
        var vecMag = Math.sqrt(vecX * vecX + vecY * vecY)

        if (root.allowModeToggle
                && distFromCentre < knobRadius * 0.9 * unit && vecMag < 0.15) {
            root.navMode = (root.navMode === NavPad.Pan) ? NavPad.Dolly : NavPad.Pan
            root.modeChanged(root.navMode)
        }

        pressed = false
        tickTimer.stop()
        resetVector()
    }

    function setMode(mode) {
        if (root.navMode !== mode) {
            root.navMode = mode
            root.modeChanged(mode)
        }
    }

    Timer {
        id: tickTimer
        interval: 16
        repeat: true
        onTriggered: {
            if (!root.pressed) return
            var vx = root.vecX
            var vy = root.vecY
            if (Math.abs(vx) < 1e-4 && Math.abs(vy) < 1e-4) return

            if (root.navMode === NavPad.Pan) {
                root.panRequested(vx, -vy)
            } else {
                root.dollyRequested(-vy, vx)
            }
        }
    }

    // ── Outer Plate: Translucent dark glass dish ───────────────────────────
    Rectangle {
        id: plate
        anchors.fill: parent
        radius: width / 2
        color: Theme.alpha(Theme.surface1, 0.72)
        border.color: root.pressed ? (isPan ? "#60A5FA" : "#FBBF24") : Theme.alpha(Theme.borderStrong, 0.65)
        border.width: 1.5

        // Crosshair lines
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.55
            height: 1
            color: Theme.alpha(Theme.borderStrong, 0.35)
        }
        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: parent.height * 0.55
            color: Theme.alpha(Theme.borderStrong, 0.35)
        }

        // 4 Directional triangle chevrons
        Icon {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: Theme.dp(4)
            name: "chevron-up"
            size: Theme.dp(12)
            color: isPan ? Theme.alpha("#93C5FD", 0.75) : Theme.alpha("#FDE68A", 0.75)
        }
        Icon {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: Theme.dp(4)
            name: "chevron-down"
            size: Theme.dp(12)
            color: isPan ? Theme.alpha("#93C5FD", 0.75) : Theme.alpha("#FDE68A", 0.75)
        }
        Icon {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.dp(4)
            name: "chevron-left"
            size: Theme.dp(12)
            color: isPan ? Theme.alpha("#93C5FD", 0.75) : Theme.alpha("#FDE68A", 0.75)
        }
        Icon {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: Theme.dp(4)
            name: "chevron-right"
            size: Theme.dp(12)
            color: isPan ? Theme.alpha("#93C5FD", 0.75) : Theme.alpha("#FDE68A", 0.75)
        }
    }

    // ── Luminous Tactile Thumb Knob ─────────────────────────────────────────
    Rectangle {
        id: knob
        x: root.knobPos.x - width / 2
        y: root.knobPos.y - height / 2
        width: Theme.dp(40)
        height: Theme.dp(40)
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: isPan ? "#4FA8FF" : "#FBBF24" }
            GradientStop { position: 0.6; color: isPan ? "#1D4ED8" : "#D97706" }
            GradientStop { position: 1.0; color: isPan ? "#172554" : "#78350F" }
        }
        border.color: isPan ? "#BFDBFE" : "#FEF08A"
        border.width: 1.5

        Column {
            anchors.centerIn: parent
            spacing: 0

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: isPan ? "move" : "compass"
                size: Theme.dp(13)
                color: "#FFFFFF"
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: isPan ? qsTr("PAN") : qsTr("FLY")
                font.pixelSize: Theme.dp(8)
                font.weight: Font.Black
                font.letterSpacing: 0.5
                color: "#FFFFFF"
            }
        }
    }

    MouseArea {
        id: padArea
        anchors.fill: parent
        preventStealing: true
        onPressed: (mouse) => root.beginPress(mouse.x, mouse.y)
        onPositionChanged: (mouse) => root.movePress(mouse.x, mouse.y)
        onReleased: (mouse) => root.endPress(mouse.x, mouse.y)
        onCanceled: root.endPress(root.width / 2, root.height / 2)
    }
}
