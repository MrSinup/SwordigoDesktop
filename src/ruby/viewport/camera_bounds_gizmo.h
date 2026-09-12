#pragma once

#include <QPointF>
#include <QRectF>
#include <QPolygonF>
#include <QString>
#include <cmath>
#include "tools/scene_loader.h"

class QPainter;

namespace ruby::viewport {

enum class BoundsHandle {
    None,
    Center,
    Left,
    Right,
    Bottom,
    Top,
    CornerBL,
    CornerBR,
    CornerTL,
    CornerTR
};

/// Interactive 2D Camera Rectangle Gizmo and Viewport Overlay.
/// Renders the game camera's level boundaries in the 3D viewport, with
/// outer dark letterbox/shroud, border, 9 interactive handles (Center + 4 Edges + 4 Corners),
/// and dimension badge.
class CameraBoundsGizmo {
public:
    CameraBoundsGizmo() = default;

    /// Hit test mouse coordinate against bounds handles.
    /// Handles are tested with handle_radius in screen space.
    BoundsHandle hit_test(const QPointF& mouse_pos,
                          const av::CameraBounds& bounds,
                          int vp_w, int vp_h,
                          const float view[16],
                          const float proj[16]);

    /// Draw the camera bounds overlay using QPainter:
    /// - Translucent dark shroud outside the camera rectangle
    /// - Vibrant cyan / gold glowing border
    /// - 4 corner grab handles and 4 edge midpoint pill handles
    /// - Center grab circle + crosshair
    /// - Dimensions badge (Width × Height)
    void draw(QPainter& p,
              const av::CameraBounds& bounds,
              int vp_w, int vp_h,
              const float view[16],
              const float proj[16],
              BoundsHandle hover,
              BoundsHandle active,
              bool selected);

    /// Unproject a screen coordinate (sx, sy) to the world XY plane (Z = 0).
    /// Returns true if the ray intersects the plane.
    static bool screen_to_world_xy(float sx, float sy,
                                   int vp_w, int vp_h,
                                   const float view[16],
                                   const float proj[16],
                                   float& out_wx, float& out_wy);

    /// Project a 3D world coordinate to 2D screen pixels.
    static bool world_to_screen(float wx, float wy, float wz,
                                int vp_w, int vp_h,
                                const float view[16],
                                const float proj[16],
                                float& out_sx, float& out_sy);

    /// Calculate dragged bounds given the initial bounds, drag handle, and world mouse delta.
    static av::CameraBounds calculate_drag(const av::CameraBounds& initial,
                                          BoundsHandle handle,
                                          float world_dx, float world_dy,
                                          float min_size = 20.0f);
};

} // namespace ruby::viewport
