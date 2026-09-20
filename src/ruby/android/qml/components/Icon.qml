import QtQuick
import QtQuick.Shapes
import ".."

// ============================================================================
// Icon.qml — the ONLY way an icon is drawn in Ruby GG Mobile.
//
// Renders Icons.path(name) as a stroked (or filled) path on a 24x24 grid,
// scaled to `size`. Colour comes from `color`, so icons inherit the theme and
// state (e.g. selected / muted / danger) without any per-icon asset.
//
//   Icon { name: "folder"; size: 20 }
//   Icon { name: "play"; filled: true; color: Theme.accentEnd }
//
// Never add a Text-based glyph or an emoji as an icon anywhere in this app.
// ============================================================================
Item {
    id: root

    property string name: ""
    property color color: Theme.textPrimary
    property real size: 22
    /// Stroke weight in 24-grid units.
    property real weight: Icons.stroke
    /// Draw as a solid shape (play, dot, star, …) instead of an outline.
    property bool filled: false

    implicitWidth: size
    implicitHeight: size
    width: implicitWidth
    height: implicitHeight

    Shape {
        anchors.fill: parent

        // Map the authored 24x24 grid onto whatever pixel size was asked for.
        transform: Scale {
            origin.x: 0
            origin.y: 0
            xScale: root.width / Icons.grid
            yScale: root.height / Icons.grid
        }

        ShapePath {
            strokeColor: root.filled ? "transparent" : root.color
            strokeWidth: root.weight
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            fillColor: root.filled ? root.color : "transparent"
            PathSvg { path: Icons.path(root.name) }
        }
    }
}
