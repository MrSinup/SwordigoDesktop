#include "camera_bounds_gizmo.h"
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QFontMetrics>
#include <algorithm>

namespace ruby::viewport {

bool CameraBoundsGizmo::world_to_screen(float wx, float wy, float wz,
                                       int vp_w, int vp_h,
                                       const float view[16],
                                       const float proj[16],
                                       float& out_sx, float& out_sy) {
    if (vp_w <= 0 || vp_h <= 0) return false;
    float vp[16] = {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                vp[col * 4 + row] += proj[k * 4 + row] * view[col * 4 + k];
            }
        }
    }
    const float world[4] = {wx, wy, wz, 1.0f};
    float cx = vp[0]*world[0] + vp[4]*world[1] + vp[8]*world[2]  + vp[12]*world[3];
    float cy = vp[1]*world[0] + vp[5]*world[1] + vp[9]*world[2]  + vp[13]*world[3];
    float cw = vp[3]*world[0] + vp[7]*world[1] + vp[11]*world[2] + vp[15]*world[3];
    if (cw <= 0.0001f) return false;
    out_sx = ( cx / cw + 1.0f) * 0.5f * static_cast<float>(vp_w);
    out_sy = (-cy / cw + 1.0f) * 0.5f * static_cast<float>(vp_h);
    return true;
}

bool CameraBoundsGizmo::screen_to_world_xy(float sx, float sy,
                                          int vp_w, int vp_h,
                                          const float view[16],
                                          const float proj[16],
                                          float& out_wx, float& out_wy) {
    if (vp_w <= 0 || vp_h <= 0) return false;
    float nx = (2.0f * sx / static_cast<float>(vp_w)) - 1.0f;
    float ny = 1.0f - (2.0f * sy / static_cast<float>(vp_h));

    float f_inv_x = (proj[0] != 0.0f) ? (1.0f / proj[0]) : 1.0f;
    float f_inv_y = (proj[5] != 0.0f) ? (1.0f / proj[5]) : 1.0f;

    float right[3]   = { view[0], view[4], view[8] };
    float up[3]      = { view[1], view[5], view[9] };
    float forward[3] = { -view[2], -view[6], -view[10] };

    float dir[3] = {
        forward[0] + right[0] * (nx * f_inv_x) + up[0] * (ny * f_inv_y),
        forward[1] + right[1] * (nx * f_inv_x) + up[1] * (ny * f_inv_y),
        forward[2] + right[2] * (nx * f_inv_x) + up[2] * (ny * f_inv_y)
    };
    float len = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
    if (len > 1e-6f) { dir[0] /= len; dir[1] /= len; dir[2] /= len; }

    float eye[3] = {
        -(view[0]*view[12] + view[1]*view[13] + view[2]*view[14]),
        -(view[4]*view[12] + view[5]*view[13] + view[6]*view[14]),
        -(view[8]*view[12] + view[9]*view[13] + view[10]*view[14])
    };

    if (std::abs(dir[2]) < 1e-6f) return false;
    float t = -eye[2] / dir[2];
    if (t < 0.0f) return false;

    out_wx = eye[0] + t * dir[0];
    out_wy = eye[1] + t * dir[1];
    return true;
}

BoundsHandle CameraBoundsGizmo::hit_test(const QPointF& mouse_pos,
                                        const av::CameraBounds& bounds,
                                        int vp_w, int vp_h,
                                        const float view[16],
                                        const float proj[16]) {
    if (!bounds.enabled || bounds.width <= 0 || bounds.height <= 0)
        return BoundsHandle::None;

    float s_bl_x, s_bl_y, s_br_x, s_br_y, s_tr_x, s_tr_y, s_tl_x, s_tl_y;
    float s_center_x, s_center_y;

    if (!world_to_screen(bounds.min_x(), bounds.min_y(), 0.0f, vp_w, vp_h, view, proj, s_bl_x, s_bl_y) ||
        !world_to_screen(bounds.max_x(), bounds.min_y(), 0.0f, vp_w, vp_h, view, proj, s_br_x, s_br_y) ||
        !world_to_screen(bounds.max_x(), bounds.max_y(), 0.0f, vp_w, vp_h, view, proj, s_tr_x, s_tr_y) ||
        !world_to_screen(bounds.min_x(), bounds.max_y(), 0.0f, vp_w, vp_h, view, proj, s_tl_x, s_tl_y) ||
        !world_to_screen(bounds.center_x(), bounds.center_y(), 0.0f, vp_w, vp_h, view, proj, s_center_x, s_center_y)) {
        return BoundsHandle::None;
    }

    auto dist_sq = [](const QPointF& p1, float x2, float y2) {
        float dx = static_cast<float>(p1.x()) - x2;
        float dy = static_cast<float>(p1.y()) - y2;
        return dx*dx + dy*dy;
    };

    auto dist_to_segment_sq = [](const QPointF& p, float x1, float y1, float x2, float y2) {
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len_sq = dx * dx + dy * dy;
        if (len_sq < 1e-4f) {
            float px = static_cast<float>(p.x()) - x1;
            float py = static_cast<float>(p.y()) - y1;
            return px * px + py * py;
        }
        float t = std::clamp(((static_cast<float>(p.x()) - x1) * dx + (static_cast<float>(p.y()) - y1) * dy) / len_sq, 0.0f, 1.0f);
        float proj_x = x1 + t * dx;
        float proj_y = y1 + t * dy;
        float px = static_cast<float>(p.x()) - proj_x;
        float py = static_cast<float>(p.y()) - proj_y;
        return px * px + py * py;
    };

    const float kCornerThreshSq   = 12.0f * 12.0f;
    const float kEdgeThreshSq     = 12.0f * 12.0f;
    const float kEdgeLineThreshSq = 8.0f * 8.0f;
    const float kCenterThreshSq   = 16.0f * 16.0f;

    // 1. Corners (highest priority)
    if (dist_sq(mouse_pos, s_bl_x, s_bl_y) <= kCornerThreshSq) return BoundsHandle::CornerBL;
    if (dist_sq(mouse_pos, s_br_x, s_br_y) <= kCornerThreshSq) return BoundsHandle::CornerBR;
    if (dist_sq(mouse_pos, s_tl_x, s_tl_y) <= kCornerThreshSq) return BoundsHandle::CornerTL;
    if (dist_sq(mouse_pos, s_tr_x, s_tr_y) <= kCornerThreshSq) return BoundsHandle::CornerTR;

    // 2. Edges midpoints
    float mid_left_x  = (s_bl_x + s_tl_x) * 0.5f;
    float mid_left_y  = (s_bl_y + s_tl_y) * 0.5f;
    float mid_right_x = (s_br_x + s_tr_x) * 0.5f;
    float mid_right_y = (s_br_y + s_tr_y) * 0.5f;
    float mid_bot_x   = (s_bl_x + s_br_x) * 0.5f;
    float mid_bot_y   = (s_bl_y + s_br_y) * 0.5f;
    float mid_top_x   = (s_tl_x + s_tr_x) * 0.5f;
    float mid_top_y   = (s_tl_y + s_tr_y) * 0.5f;

    if (dist_sq(mouse_pos, mid_left_x, mid_left_y) <= kEdgeThreshSq) return BoundsHandle::Left;
    if (dist_sq(mouse_pos, mid_right_x, mid_right_y) <= kEdgeThreshSq) return BoundsHandle::Right;
    if (dist_sq(mouse_pos, mid_bot_x, mid_bot_y) <= kEdgeThreshSq) return BoundsHandle::Bottom;
    if (dist_sq(mouse_pos, mid_top_x, mid_top_y) <= kEdgeThreshSq) return BoundsHandle::Top;

    // 3. Center grab icon (only within 16px of center handle, never whole interior)
    if (dist_sq(mouse_pos, s_center_x, s_center_y) <= kCenterThreshSq) return BoundsHandle::Center;

    // 4. Test perimeter edge segments (clicking near border lines)
    if (dist_to_segment_sq(mouse_pos, s_bl_x, s_bl_y, s_tl_x, s_tl_y) <= kEdgeLineThreshSq) return BoundsHandle::Left;
    if (dist_to_segment_sq(mouse_pos, s_br_x, s_br_y, s_tr_x, s_tr_y) <= kEdgeLineThreshSq) return BoundsHandle::Right;
    if (dist_to_segment_sq(mouse_pos, s_bl_x, s_bl_y, s_br_x, s_br_y) <= kEdgeLineThreshSq) return BoundsHandle::Bottom;
    if (dist_to_segment_sq(mouse_pos, s_tl_x, s_tl_y, s_tr_x, s_tr_y) <= kEdgeLineThreshSq) return BoundsHandle::Top;

    return BoundsHandle::None;
}

void CameraBoundsGizmo::draw(QPainter& p,
                             const av::CameraBounds& bounds,
                             int vp_w, int vp_h,
                             const float view[16],
                             const float proj[16],
                             BoundsHandle hover,
                             BoundsHandle active,
                             bool selected) {
    if (!bounds.enabled || bounds.width <= 0 || bounds.height <= 0) return;

    float s_bl_x, s_bl_y, s_br_x, s_br_y, s_tr_x, s_tr_y, s_tl_x, s_tl_y;
    float s_center_x, s_center_y;

    if (!world_to_screen(bounds.min_x(), bounds.min_y(), 0.0f, vp_w, vp_h, view, proj, s_bl_x, s_bl_y) ||
        !world_to_screen(bounds.max_x(), bounds.min_y(), 0.0f, vp_w, vp_h, view, proj, s_br_x, s_br_y) ||
        !world_to_screen(bounds.max_x(), bounds.max_y(), 0.0f, vp_w, vp_h, view, proj, s_tr_x, s_tr_y) ||
        !world_to_screen(bounds.min_x(), bounds.max_y(), 0.0f, vp_w, vp_h, view, proj, s_tl_x, s_tl_y) ||
        !world_to_screen(bounds.center_x(), bounds.center_y(), 0.0f, vp_w, vp_h, view, proj, s_center_x, s_center_y)) {
        return;
    }

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    QPolygonF quad;
    quad << QPointF(s_bl_x, s_bl_y) << QPointF(s_br_x, s_br_y)
         << QPointF(s_tr_x, s_tr_y) << QPointF(s_tl_x, s_tl_y);

    // ── 1. Shaded Letterbox / Dark Shroud outside camera frame ──
    QPainterPath full_screen;
    full_screen.addRect(0, 0, vp_w, vp_h);
    QPainterPath camera_rect;
    camera_rect.addPolygon(quad);
    QPainterPath shroud = full_screen.subtracted(camera_rect);

    p.fillPath(shroud, QColor(10, 14, 20, selected ? 95 : 75));

    // ── 2. Glowing Rectangle Border ──
    const bool is_active = (active != BoundsHandle::None);
    const bool is_hovered = (hover != BoundsHandle::None);
    const QColor base_color = (selected || is_active)
                                  ? QColor(245, 158, 11)   // vibrant amber gold
                                  : (is_hovered ? QColor(56, 189, 248) : QColor(6, 182, 212)); // cyan

    // Soft outer glow line
    QPen glow_pen(QColor(base_color.red(), base_color.green(), base_color.blue(), 60), 5.0);
    p.setPen(glow_pen);
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(quad);

    // Core crisp outline
    QPen border_pen(base_color, (selected || is_active || is_hovered) ? 2.5 : 1.8);
    p.setPen(border_pen);
    p.drawPolygon(quad);

    // Subtle inner grid rule (3x3 camera rule of thirds) when selected
    if (selected || is_active) {
        QPen rule_pen(QColor(base_color.red(), base_color.green(), base_color.blue(), 45), 1.0, Qt::DashLine);
        p.setPen(rule_pen);
        for (int i = 1; i <= 2; ++i) {
            float f = i / 3.0f;
            float h1_x = s_tl_x * (1 - f) + s_bl_x * f;
            float h1_y = s_tl_y * (1 - f) + s_bl_y * f;
            float h2_x = s_tr_x * (1 - f) + s_br_x * f;
            float h2_y = s_tr_y * (1 - f) + s_br_y * f;
            p.drawLine(QPointF(h1_x, h1_y), QPointF(h2_x, h2_y));

            float v1_x = s_tl_x * (1 - f) + s_tr_x * f;
            float v1_y = s_tl_y * (1 - f) + s_tr_y * f;
            float v2_x = s_bl_x * (1 - f) + s_br_x * f;
            float v2_y = s_bl_y * (1 - f) + s_br_y * f;
            p.drawLine(QPointF(v1_x, v1_y), QPointF(v2_x, v2_y));
        }
    }

    // ── 3. Edge Midpoint Grab Handles (Pills) ──
    auto draw_edge_pill = [&](float px, float py, BoundsHandle handle_id, bool vert) {
        bool hov = (hover == handle_id);
        bool act = (active == handle_id);
        float pw = vert ? 6.0f : 16.0f;
        float ph = vert ? 16.0f : 6.0f;
        QRectF r(px - pw * 0.5f, py - ph * 0.5f, pw, ph);

        QColor fill = act ? QColor(255, 255, 255) : (hov ? QColor(245, 158, 11) : base_color);
        p.setPen(QPen(QColor(15, 23, 42, 220), 1.2));
        p.setBrush(fill);
        p.drawRoundedRect(r, 2.5, 2.5);
    };

    float mid_left_x  = (s_bl_x + s_tl_x) * 0.5f;
    float mid_left_y  = (s_bl_y + s_tl_y) * 0.5f;
    float mid_right_x = (s_br_x + s_tr_x) * 0.5f;
    float mid_right_y = (s_br_y + s_tr_y) * 0.5f;
    float mid_bot_x   = (s_bl_x + s_br_x) * 0.5f;
    float mid_bot_y   = (s_bl_y + s_br_y) * 0.5f;
    float mid_top_x   = (s_tl_x + s_tr_x) * 0.5f;
    float mid_top_y   = (s_tl_y + s_tr_y) * 0.5f;

    draw_edge_pill(mid_left_x, mid_left_y, BoundsHandle::Left, true);
    draw_edge_pill(mid_right_x, mid_right_y, BoundsHandle::Right, true);
    draw_edge_pill(mid_bot_x, mid_bot_y, BoundsHandle::Bottom, false);
    draw_edge_pill(mid_top_x, mid_top_y, BoundsHandle::Top, false);

    // ── 4. Corner Handles (Rounded Squares) ──
    auto draw_corner_handle = [&](float px, float py, BoundsHandle handle_id) {
        bool hov = (hover == handle_id);
        bool act = (active == handle_id);
        const float hs = (hov || act) ? 10.0f : 8.5f;
        QRectF r(px - hs * 0.5f, py - hs * 0.5f, hs, hs);

        QColor fill = act ? QColor(255, 255, 255) : (hov ? QColor(245, 158, 11) : QColor(248, 250, 252));
        p.setPen(QPen(base_color, 1.5));
        p.setBrush(fill);
        p.drawRoundedRect(r, 2.0, 2.0);
    };

    draw_corner_handle(s_bl_x, s_bl_y, BoundsHandle::CornerBL);
    draw_corner_handle(s_br_x, s_br_y, BoundsHandle::CornerBR);
    draw_corner_handle(s_tl_x, s_tl_y, BoundsHandle::CornerTL);
    draw_corner_handle(s_tr_x, s_tr_y, BoundsHandle::CornerTR);

    // ── 5. Center Move Crosshair + Position Readout ──
    {
        bool c_hov = (hover == BoundsHandle::Center);
        bool c_act = (active == BoundsHandle::Center);
        float cr = (c_hov || c_act) ? 9.0f : 7.0f;
        p.setPen(QPen(QColor(15, 23, 42, 200), 1.5));
        p.setBrush(c_act ? QColor(245, 158, 11) : (c_hov ? QColor(255, 255, 255) : base_color));
        p.drawEllipse(QPointF(s_center_x, s_center_y), cr, cr);

        // Crosshair reticle
        p.setPen(QPen(c_act ? QColor(15, 23, 42) : QColor(255, 255, 255), 1.2));
        p.drawLine(QPointF(s_center_x - cr * 0.5f, s_center_y), QPointF(s_center_x + cr * 0.5f, s_center_y));
        p.drawLine(QPointF(s_center_x, s_center_y - cr * 0.5f), QPointF(s_center_x, s_center_y + cr * 0.5f));
    }

    // ── 6. Dimension Badge at Top Edge ──
    {
        QString w_str = (std::abs(bounds.width - std::round(bounds.width)) < 0.05f)
                            ? QString::number(static_cast<int>(std::round(bounds.width)))
                            : QString::number(bounds.width, 'f', 1);
        QString h_str = (std::abs(bounds.height - std::round(bounds.height)) < 0.05f)
                            ? QString::number(static_cast<int>(std::round(bounds.height)))
                            : QString::number(bounds.height, 'f', 1);
        QString badge = QStringLiteral("Camera Bounds: %1 × %2").arg(w_str, h_str);

        QFont font = p.font();
        font.setPixelSize(11);
        font.setBold(true);
        p.setFont(font);

        QFontMetrics fm(font);
        int text_w = fm.horizontalAdvance(badge);
        int text_h = fm.height();
        float badge_w = static_cast<float>(text_w + 20);
        float badge_h = static_cast<float>(text_h + 8);

        // Anchor badge above the visual top of the bounds, or pin safely inside visible viewport
        float top_screen_y = std::min({s_tl_y, s_tr_y, mid_top_y});
        float badge_x = std::round(std::clamp(s_center_x - badge_w * 0.5f, 12.0f, static_cast<float>(vp_w) - badge_w - 12.0f));
        float badge_y = std::round(top_screen_y - badge_h - 10.0f);

        if (badge_y < 46.0f) {
            if (top_screen_y > 46.0f + badge_h + 16.0f && top_screen_y < static_cast<float>(vp_h)) {
                badge_y = std::round(top_screen_y + 16.0f); // Inside rectangle below top handle
            } else {
                badge_y = 46.0f; // Pinned safely below gizmo bar
            }
        }
        badge_y = std::clamp(badge_y, 46.0f, static_cast<float>(vp_h) - badge_h - 12.0f);

        QRectF badge_rect(badge_x, badge_y, badge_w, badge_h);

        p.setPen(QPen(base_color, 1.2));
        p.setBrush(QColor(15, 23, 42, 235));
        p.drawRoundedRect(badge_rect, 4.0, 4.0);

        p.setPen(QColor(241, 245, 249));
        p.drawText(badge_rect, Qt::AlignCenter, badge);
    }

    p.restore();
}

av::CameraBounds CameraBoundsGizmo::calculate_drag(const av::CameraBounds& initial,
                                                   BoundsHandle handle,
                                                   float world_dx, float world_dy,
                                                   float min_size) {
    av::CameraBounds b = initial;
    float new_min_x = b.x;
    float new_max_x = b.x + b.width;
    float new_min_y = b.y;
    float new_max_y = b.y + b.height;

    switch (handle) {
        case BoundsHandle::Center:
            b.x += world_dx;
            b.y += world_dy;
            return b;

        case BoundsHandle::Left:
            new_min_x += world_dx;
            if (new_max_x - new_min_x < min_size) new_min_x = new_max_x - min_size;
            break;

        case BoundsHandle::Right:
            new_max_x += world_dx;
            if (new_max_x - new_min_x < min_size) new_max_x = new_min_x + min_size;
            break;

        case BoundsHandle::Bottom:
            new_min_y += world_dy;
            if (new_max_y - new_min_y < min_size) new_min_y = new_max_y - min_size;
            break;

        case BoundsHandle::Top:
            new_max_y += world_dy;
            if (new_max_y - new_min_y < min_size) new_max_y = new_min_y + min_size;
            break;

        case BoundsHandle::CornerBL:
            new_min_x += world_dx;
            new_min_y += world_dy;
            if (new_max_x - new_min_x < min_size) new_min_x = new_max_x - min_size;
            if (new_max_y - new_min_y < min_size) new_min_y = new_max_y - min_size;
            break;

        case BoundsHandle::CornerBR:
            new_max_x += world_dx;
            new_min_y += world_dy;
            if (new_max_x - new_min_x < min_size) new_max_x = new_min_x + min_size;
            if (new_max_y - new_min_y < min_size) new_min_y = new_max_y - min_size;
            break;

        case BoundsHandle::CornerTL:
            new_min_x += world_dx;
            new_max_y += world_dy;
            if (new_max_x - new_min_x < min_size) new_min_x = new_max_x - min_size;
            if (new_max_y - new_min_y < min_size) new_max_y = new_min_y + min_size;
            break;

        case BoundsHandle::CornerTR:
            new_max_x += world_dx;
            new_max_y += world_dy;
            if (new_max_x - new_min_x < min_size) new_max_x = new_min_x + min_size;
            if (new_max_y - new_min_y < min_size) new_max_y = new_min_y + min_size;
            break;

        default:
            break;
    }

    b.x = new_min_x;
    b.y = new_min_y;
    b.width = new_max_x - new_min_x;
    b.height = new_max_y - new_min_y;
    return b;
}

} // namespace ruby::viewport
