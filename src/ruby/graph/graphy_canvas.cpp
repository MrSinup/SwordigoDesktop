// ============================================================================
// graphy_canvas.cpp — Unreal-Style Node Graph Canvas Widget for Ruby GG
// Inspired by Unreal Engine SGraphPanel / SGraphNode and Godot GraphEdit
// ============================================================================

#include "graphy_canvas.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QApplication>
#include <cmath>
#include <algorithm>
#include <utility>

namespace ruby::graph {

// ── Constructor ──────────────────────────────────────────────────────────────

GraphyCanvas::GraphyCanvas(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);

    m_measure = default_text_measure();

    // Initial default demo showcase graph
    m_graph = Graph::create_demo_graph();
}

void GraphyCanvas::set_graph(std::shared_ptr<Graph> g) {
    m_graph = g ? g : std::make_shared<Graph>();
    clear_selection();
    update();
    emit graph_modified();
}

void GraphyCanvas::clear_selection() {
    m_selected_node_ids.clear();
    m_selected_comment_ids.clear();
    m_selected_conn_id = 0;
    update();
    emit selection_changed();
}

void GraphyCanvas::frame_all() {
    if (!m_graph || m_graph->nodes().empty()) {
        m_pan_x = 100.0f;
        m_pan_y = 100.0f;
        m_zoom  = 1.0f;
        update();
        return;
    }

    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    for (const auto& n : m_graph->nodes()) {
        if (!n) continue;
        min_x = std::min(min_x, n->x());
        min_y = std::min(min_y, n->y());
        max_x = std::max(max_x, n->x() + n->width());
        max_y = std::max(max_y, n->y() + n->height());
    }

    for (const auto& c : m_graph->comments()) {
        min_x = std::min(min_x, c.x);
        min_y = std::min(min_y, c.y);
        max_x = std::max(max_x, c.x + c.width);
        max_y = std::max(max_y, c.y + c.height);
    }

    float pad = 60.0f;
    min_x -= pad; min_y -= pad;
    max_x += pad; max_y += pad;

    float gw = std::max(100.0f, max_x - min_x);
    float gh = std::max(100.0f, max_y - min_y);

    float zx = width()  / gw;
    float zy = height() / gh;
    // The old floor of 0.25 meant "frame all" could not fit a graph wider than
    // 4x the viewport, which is most real scene graphs, so F left the view
    // somewhere that still showed a fraction of the nodes. Zoom out as far as
    // fitting actually requires; only cap the zoom-*in* case.
    m_zoom = std::clamp(std::min(zx, zy), 0.02f, 1.0f);

    m_pan_x = (width()  - gw * m_zoom) * 0.5f - min_x * m_zoom;
    m_pan_y = (height() - gh * m_zoom) * 0.5f - min_y * m_zoom;

    update();
}

// ── Coordinate Transformations ───────────────────────────────────────────────

QPointF GraphyCanvas::screen_to_canvas(const QPointF& sp) const {
    return QPointF((sp.x() - m_pan_x) / m_zoom, (sp.y() - m_pan_y) / m_zoom);
}

QPointF GraphyCanvas::canvas_to_screen(const QPointF& cp) const {
    return QPointF(cp.x() * m_zoom + m_pan_x, cp.y() * m_zoom + m_pan_y);
}

QRectF GraphyCanvas::screen_to_canvas_rect(const QRectF& sr) const {
    QPointF tl = screen_to_canvas(sr.topLeft());
    QPointF br = screen_to_canvas(sr.bottomRight());
    return QRectF(tl, br).normalized();
}

QRectF GraphyCanvas::canvas_to_screen_rect(const QRectF& cr) const {
    QPointF tl = canvas_to_screen(cr.topLeft());
    QPointF br = canvas_to_screen(cr.bottomRight());
    return QRectF(tl, br).normalized();
}

// ── Layout & Node Geometry ───────────────────────────────────────────────────

void GraphyCanvas::update_node_layout(Node& node) {
    const TextMeasure& measure = m_measure;
    const PinConnectedFn connected = [this](int pin_id) {
        return m_graph && !m_graph->connections_for_pin(pin_id).empty();
    };

    NodeGeometry geo = compute_node_geometry(node, m_metrics, measure, connected);

    // The layout engine is the single authority for node size. Sizing from the
    // node's own measured text is what removes the dead band that came from
    // `max(json_height, pin_count_height)`, and stops a long subtitle from
    // being cut off by a card that was sized for its pin count alone.
    node.set_size(geo.card.w, geo.card.h);

    for (size_t i = 0; i < node.inputs().size() && i < geo.inputs.size(); ++i) {
        node.inputs_mut()[i].canvas_x = geo.inputs[i].pin_x;
        node.inputs_mut()[i].canvas_y = geo.inputs[i].y;
    }
    for (size_t i = 0; i < node.outputs().size() && i < geo.outputs.size(); ++i) {
        node.outputs_mut()[i].canvas_x = geo.outputs[i].pin_x;
        node.outputs_mut()[i].canvas_y = geo.outputs[i].y;
    }

    m_geometry[node.id()] = std::move(geo);
}

void GraphyCanvas::relayout_all() {
    m_geometry.clear();
    if (!m_graph) return;
    for (auto& n : m_graph->nodes()) {
        if (n) update_node_layout(*n);
    }
}

void GraphyCanvas::set_metrics(const LayoutMetrics& m) {
    m_metrics = m;
    relayout_all();
    update();
}

void GraphyCanvas::set_text_measure(TextMeasure measure) {
    m_measure = measure ? std::move(measure) : default_text_measure();
    relayout_all();
    update();
}

const NodeGeometry* GraphyCanvas::node_geometry(int node_id) const {
    const auto it = m_geometry.find(node_id);
    return (it == m_geometry.end()) ? nullptr : &it->second;
}

void GraphyCanvas::set_minimap_visible(bool visible) {
    if (m_show_minimap == visible) return;
    m_show_minimap = visible;
    update();
}

void GraphyCanvas::toggle_minimap() {
    set_minimap_visible(!m_show_minimap);
}

// ── Spline Path Builder (Unreal horizontal Bézier + Godot loopback logic) ─────

QPainterPath GraphyCanvas::make_spline_path(const QPointF& p0, const QPointF& p1) const {
    QPainterPath path;
    path.moveTo(p0);

    float dx = p1.x() - p0.x();
    // Base curvature offset
    float offset = std::max(35.0f, std::abs(dx) * 0.50f);

    // Godot-style loopback handling: if wire loops back to the left, extend control points outward
    if (dx < 0.0f) {
        offset = std::max(60.0f, std::abs(dx) * 0.35f + 50.0f);
    }

    QPointF c1(p0.x() + offset, p0.y());
    QPointF c2(p1.x() - offset, p1.y());

    path.cubicTo(c1, c2, p1);
    return path;
}

// ── Hit Testing ──────────────────────────────────────────────────────────────

int GraphyCanvas::hit_test_pin(const QPointF& cp) const {
    if (!m_graph) return 0;
    const float pin_radius = 12.0f; // generous hit radius

    for (const auto& n : m_graph->nodes()) {
        if (!n) continue;
        for (const auto& p : n->inputs()) {
            float dx = p.canvas_x - cp.x();
            float dy = p.canvas_y - cp.y();
            if (dx*dx + dy*dy <= pin_radius * pin_radius) return p.id;
        }
        for (const auto& p : n->outputs()) {
            float dx = p.canvas_x - cp.x();
            float dy = p.canvas_y - cp.y();
            if (dx*dx + dy*dy <= pin_radius * pin_radius) return p.id;
        }
    }
    return 0;
}

std::shared_ptr<Node> GraphyCanvas::hit_test_node(const QPointF& cp) const {
    if (!m_graph) return nullptr;
    for (auto it = m_graph->nodes().rbegin(); it != m_graph->nodes().rend(); ++it) {
        auto n = *it;
        if (!n) continue;
        QRectF r(n->x(), n->y(), n->width(), n->height());
        if (r.contains(cp)) return n;
    }
    return nullptr;
}

int GraphyCanvas::hit_test_connection(const QPointF& cp, float tolerance) const {
    if (!m_graph) return 0;

    for (const auto& c : m_graph->connections()) {
        const Pin* p0 = m_graph->find_pin(c.from_pin);
        const Pin* p1 = m_graph->find_pin(c.to_pin);
        if (!p0 || !p1) continue;

        QPointF a(p0->canvas_x, p0->canvas_y);
        QPointF b(p1->canvas_x, p1->canvas_y);
        QPainterPath path = make_spline_path(a, b);

        const int steps = 24;
        for (int i = 0; i <= steps; ++i) {
            qreal t = (qreal)i / (qreal)steps;
            QPointF pt = path.pointAtPercent(t);
            float dx = pt.x() - cp.x();
            float dy = pt.y() - cp.y();
            if (std::hypot(dx, dy) <= tolerance) {
                return c.id;
            }
        }
    }
    return 0;
}

int GraphyCanvas::hit_test_comment(const QPointF& cp, bool& out_resize_handle) const {
    out_resize_handle = false;
    if (!m_graph) return 0;

    for (auto it = m_graph->comments().rbegin(); it != m_graph->comments().rend(); ++it) {
        const auto& c = *it;
        QRectF r(c.x, c.y, c.width, c.height);
        QRectF resize_r(c.x + c.width - 18.0f, c.y + c.height - 18.0f, 18.0f, 18.0f);
        if (resize_r.contains(cp)) {
            out_resize_handle = true;
            return c.id;
        }
        if (r.contains(cp)) {
            return c.id;
        }
    }
    return 0;
}

// ── Paint Pass ───────────────────────────────────────────────────────────────

void GraphyCanvas::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 1. Dark Blueprint Background & Dot Grid
    draw_grid(p);

    // Save world screen transform
    p.save();
    p.translate(m_pan_x, m_pan_y);
    p.scale(m_zoom, m_zoom);

    // 1b. One layout pass: computes and caches every node's geometry, so the
    //     painter and hit-testing can never disagree about where a rect is.
    relayout_all();

    // 2. Comments / Groups (drawn behind nodes)
    draw_comments(p);

    // 3. Connections (cables)
    draw_connections(p);

    // 4. Live Wire being dragged
    draw_active_wire(p);

    // 5. Node Cards
    draw_nodes(p);

    // Wire Slicing Laser Cutter (Unreal Alt+Drag)
    if (m_drag_mode == DragMode::SliceWires) {
        draw_slice_line(p);
    }

    p.restore();

    // 6. Screen-Space Marquee Box
    if (m_drag_mode == DragMode::Marquee) {
        draw_marquee(p);
    }

    // 7. HUD Minimap
    if (m_show_minimap) {
        draw_minimap(p);
    }
}

void GraphyCanvas::draw_grid(QPainter& p) {
    p.fillRect(rect(), QColor(22, 23, 26)); // Unreal dark graphite

    float grid_step = 20.0f * m_zoom;
    if (grid_step < 5.0f) return;

    float start_x = std::fmod(m_pan_x, grid_step);
    if (start_x < 0.0f) start_x += grid_step;
    float start_y = std::fmod(m_pan_y, grid_step);
    if (start_y < 0.0f) start_y += grid_step;

    float major_step = 100.0f * m_zoom;
    float major_start_x = std::fmod(m_pan_x, major_step);
    if (major_start_x < 0.0f) major_start_x += major_step;
    float major_start_y = std::fmod(m_pan_y, major_step);
    if (major_start_y < 0.0f) major_start_y += major_step;

    // Draw major grid lines (subtle blueprint grid)
    p.setPen(QPen(QColor(38, 40, 48, 120), 1.0f));
    for (float x = major_start_x; x < width(); x += major_step) {
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
    }
    for (float y = major_start_y; y < height(); y += major_step) {
        p.drawLine(QPointF(0, y), QPointF(width(), y));
    }

    // Draw minor dots
    if (m_zoom > 0.45f) {
        p.setPen(QPen(QColor(52, 55, 66, 180), 1.5f));
        for (float x = start_x; x < width(); x += grid_step) {
            for (float y = start_y; y < height(); y += grid_step) {
                p.drawPoint(QPointF(x, y));
            }
        }
    }
}

void GraphyCanvas::draw_comments(QPainter& p) {
    if (!m_graph) return;

    for (const auto& c : m_graph->comments()) {
        bool selected = (m_selected_comment_ids.count(c.id) > 0);
        QRectF r(c.x, c.y, c.width, c.height);

        // Body fill
        p.setPen(Qt::NoPen);
        p.setBrush(c.color);
        p.drawRoundedRect(r, 6.0f, 6.0f);

        // Banner header (Unreal TitleBarColorMultiplier = 0.6f)
        QRectF header_r(c.x, c.y, c.width, 26.0f);
        QColor header_col = c.color;
        header_col.setAlpha(std::min(255, header_col.alpha() + 60));
        p.setBrush(header_col);
        p.drawRoundedRect(header_r, 6.0f, 6.0f);
        p.drawRect(QRectF(c.x, c.y + 20.0f, c.width, 6.0f));

        // Outline
        QPen outline(selected ? QColor(240, 190, 40) : QColor(c.color.lighter(130)), selected ? 2.0f : 1.0f);
        p.setPen(outline);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, 6.0f, 6.0f);

        // Title text
        p.setPen(QColor(230, 235, 245));
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(12);
        p.setFont(f);
        p.drawText(header_r.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, c.title);

        // Resize corner handle
        QRectF resize_r(c.x + c.width - 12.0f, c.y + c.height - 12.0f, 10.0f, 10.0f);
        p.setPen(QColor(180, 190, 210, 150));
        p.drawLine(resize_r.bottomLeft(), resize_r.topRight());
        p.drawLine(resize_r.topLeft() + QPointF(4, 0), resize_r.bottomRight() - QPointF(0, 4));
    }
}

void GraphyCanvas::draw_connections(QPainter& p) {
    if (!m_graph) return;

    for (const auto& c : m_graph->connections()) {
        const Pin* p0 = m_graph->find_pin(c.from_pin);
        const Pin* p1 = m_graph->find_pin(c.to_pin);
        if (!p0 || !p1) continue;

        bool is_selected = (c.id == m_selected_conn_id);
        bool is_hovered  = (c.id == m_hovered_conn);

        QPointF a(p0->canvas_x, p0->canvas_y);
        QPointF b(p1->canvas_x, p1->canvas_y);
        QPainterPath path = make_spline_path(a, b);

        float width_base = (p0->type == PinType::Exec) ? 3.0f : 2.2f;

        if (is_selected) {
            p.setPen(QPen(QColor(240, 190, 40, 100), width_base + 4.0f));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        } else if (is_hovered) {
            p.setPen(QPen(pin_type_color(p0->type).lighter(140), width_base + 3.0f));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }

        // Godot-style color gradient interpolation along wire if types differ
        if (p0->type != p1->type && !is_selected) {
            QLinearGradient grad(a, b);
            grad.setColorAt(0.0, pin_type_color(p0->type));
            grad.setColorAt(1.0, pin_type_color(p1->type));
            p.setPen(QPen(QBrush(grad), width_base));
        } else {
            QColor col = is_selected ? QColor(255, 220, 80) : pin_type_color(p0->type);
            p.setPen(QPen(col, width_base));
        }

        p.setBrush(Qt::NoBrush);
        p.drawPath(path);

        // Unreal DrawSplineWithArrow: draw arrow glyph on Exec wires
        if (p0->type == PinType::Exec) {
            QPointF mid = path.pointAtPercent(0.5);
            QPointF mid_next = path.pointAtPercent(0.53);
            float ang = std::atan2(mid_next.y() - mid.y(), mid_next.x() - mid.x());

            QPainterPath arrow;
            arrow.moveTo(mid);
            arrow.lineTo(mid.x() - 7.0f * std::cos(ang - 0.5f), mid.y() - 7.0f * std::sin(ang - 0.5f));
            arrow.lineTo(mid.x() - 7.0f * std::cos(ang + 0.5f), mid.y() - 7.0f * std::sin(ang + 0.5f));
            arrow.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255));
            p.drawPath(arrow);
        }
    }
}

void GraphyCanvas::draw_active_wire(QPainter& p) {
    if (m_drag_mode != DragMode::DrawWire || !m_graph) return;
    const Pin* pin = m_graph->find_pin(m_wire_from_pin);
    if (!pin) return;

    QPointF start(pin->canvas_x, pin->canvas_y);
    QPointF end = m_wire_cur_pos;

    // Magnetic snap if hovering a pin
    if (m_hover_snap_pin > 0) {
        if (const Pin* sp = m_graph->find_pin(m_hover_snap_pin)) {
            end = QPointF(sp->canvas_x, sp->canvas_y);
        }
    }

    QPainterPath path = (pin->dir == PinDirection::Output)
                            ? make_spline_path(start, end)
                            : make_spline_path(end, start);

    QColor col = pin_type_color(pin->type);
    p.setPen(QPen(col.lighter(130), 2.5f, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void GraphyCanvas::draw_nodes(QPainter& p) {
    if (!m_graph) return;

    for (const auto& n : m_graph->nodes()) {
        if (!n) continue;

        bool is_selected = (m_selected_node_ids.count(n->id()) > 0);
        bool is_hovered  = (m_hovered_node && m_hovered_node->id() == n->id());

        // Geometry is produced once by the layout engine (graphy_layout.cpp)
        // and cached, so the painter draws the same rects the layout pass
        // measured. Every text rect below is content-sized and pre-elided.
        const NodeGeometry* geo = node_geometry(n->id());
        if (!geo) {
            update_node_layout(*n);
            geo = node_geometry(n->id());
            if (!geo) continue;
        }

        // ── Reroute Knot Pin (UK2Node_Knot) ───────────────────────────────────
        if (n->flags() & NodeFlags::Reroute) {
            QColor knot_col = (!n->outputs().empty()) ? pin_type_color(n->outputs()[0].type) : QColor(200, 200, 200);
            QPointF centre(n->x() + 12.0f, n->y() + 12.0f);

            p.setPen(QPen(is_selected ? QColor(240, 190, 40) : QColor(30, 32, 38), is_selected ? 2.5f : 1.5f));
            p.setBrush(knot_col);
            p.drawEllipse(centre, 6.0f, 6.0f);
            continue;
        }

        // ── Standard Unreal Node Card (SGraphNode) ────────────────────────────
        QRectF card_rect(geo->card.x, geo->card.y, geo->card.w, geo->card.h);

        // Drop Shadow
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 85));
        p.drawRoundedRect(card_rect.translated(2.0f, 4.0f), 8.0f, 8.0f);

        // Body
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(27, 28, 33)); // #1B1C21
        p.drawRoundedRect(card_rect, 8.0f, 8.0f);

        // Header gradient — its height is title band + (subtitle band if any),
        // so a node with no subtitle gets a shorter header rather than a dead
        // band, and a node with one never overlaps it with the title.
        QRectF header_rect(n->x(), n->y(), geo->card.w, geo->header_h);
        QLinearGradient grad(header_rect.topLeft(), header_rect.bottomLeft());

        if (n->flags() & NodeFlags::Event) {
            grad.setColorAt(0.0, QColor(160, 37, 37));
            grad.setColorAt(1.0, QColor(90, 20, 20));
        } else if (n->flags() & NodeFlags::Branch) {
            grad.setColorAt(0.0, QColor(180, 100, 25));
            grad.setColorAt(1.0, QColor(100, 50, 10));
        } else if (n->flags() & NodeFlags::PureFunction) {
            grad.setColorAt(0.0, QColor(30, 120, 80));
            grad.setColorAt(1.0, QColor(16, 64, 48));
        } else if (n->flags() & NodeFlags::Subgraph) {
            grad.setColorAt(0.0, QColor(100, 40, 130));
            grad.setColorAt(1.0, QColor(50, 20, 70));
        } else {
            // Function / Action default
            grad.setColorAt(0.0, QColor(30, 80, 139));
            grad.setColorAt(1.0, QColor(16, 43, 80));
        }

        p.setBrush(grad);
        p.drawRoundedRect(header_rect, 8.0f, 8.0f);
        p.drawRect(QRectF(n->x(), n->y() + geo->header_h - 8.0f, geo->card.w, 8.0f));

        // Border / Halo
        QColor border_col = is_selected ? QColor(240, 190, 40)
                                        : (is_hovered ? QColor(80, 85, 105) : QColor(43, 45, 53));
        p.setPen(QPen(border_col, is_selected ? 2.5f : 1.0f));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(card_rect, 8.0f, 8.0f);

        // Header Title — drawn inside its own reserved band, at the same pixel
        // size the layout engine measured it with.
        p.setPen(QColor(240, 242, 248));
        QFont f_title = p.font();
        f_title.setBold(true);
        f_title.setPixelSize(m_metrics.title_px);
        p.setFont(f_title);
        p.drawText(QRectF(geo->title_band.x, geo->title_band.y,
                          geo->title_band.w, geo->title_band.h),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   elide_to_width(n->title(), geo->title_band.w, m_metrics.title_px,
                                  true, false, m_measure));

        // Header Subtitle — its own band *below* the title. Previously this was
        // the title band nudged down by 14px inside the same 32px strip, which
        // is why every node in the reported screenshot collided.
        if (geo->has_subtitle) {
            p.setPen(QColor(180, 190, 210, 180));
            QFont f_sub = p.font();
            f_sub.setBold(false);
            f_sub.setItalic(true);
            f_sub.setPixelSize(m_metrics.subtitle_px);
            p.setFont(f_sub);
            p.drawText(QRectF(geo->subtitle_band.x, geo->subtitle_band.y,
                              geo->subtitle_band.w, geo->subtitle_band.h),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       elide_to_width(n->subtitle(), geo->subtitle_band.w,
                                      m_metrics.subtitle_px, false, true, m_measure));
        }

        // ── Pins & Labels ─────────────────────────────────────────────────────
        QFont f_pin = p.font();
        f_pin.setBold(false);
        f_pin.setItalic(false);
        f_pin.setPixelSize(m_metrics.label_px);
        p.setFont(f_pin);

        // Input pins
        for (size_t pi = 0; pi < n->inputs().size() && pi < geo->inputs.size(); ++pi) {
            const Pin& pin = n->inputs()[pi];
            const PinGeometry& pg = geo->inputs[pi];
            bool conn = (!m_graph->connections_for_pin(pin.id).empty());
            QColor pcol = pin_type_color(pin.type);
            QPointF p_pos(pg.pin_x, pg.y);

            if (pin.type == PinType::Exec) {
                QPainterPath arrow;
                arrow.moveTo(p_pos.x() - 4.0f, p_pos.y() - 6.0f);
                arrow.lineTo(p_pos.x() + 4.0f, p_pos.y());
                arrow.lineTo(p_pos.x() - 4.0f, p_pos.y() + 6.0f);
                arrow.closeSubpath();
                p.setPen(QPen(pcol, 1.2f));
                p.setBrush(conn ? pcol : Qt::NoBrush);
                p.drawPath(arrow);
            } else {
                p.setPen(QPen(pcol, 1.5f));
                p.setBrush(conn ? pcol : Qt::NoBrush);
                p.drawEllipse(p_pos, 5.0f, 5.0f);
            }

            // Label rect is exactly the measured text, so it can never be
            // clipped and can never run under the pill.
            p.setPen(QColor(215, 220, 230));
            if (!pg.label_text.isEmpty()) {
                p.drawText(QRectF(pg.label_rect.x, pg.label_rect.y, pg.label_rect.w, pg.label_rect.h),
                           Qt::AlignVCenter | Qt::AlignLeft, pg.label_text);
            }

            // Inline value pill — content-sized and elided, never a fixed 48px
            // box that cuts a vector mid-character.
            if (pg.has_pill && !conn) {
                const QRectF pill_r(pg.pill_rect.x, pg.pill_rect.y, pg.pill_rect.w, pg.pill_rect.h);
                p.setPen(QPen(QColor(55, 58, 68), 1.0f));
                p.setBrush(QColor(18, 19, 22));
                p.drawRoundedRect(pill_r, 3.0f, 3.0f);

                p.setPen(QColor(140, 145, 160));
                QFont f_val = f_pin;
                f_val.setPixelSize(m_metrics.value_px);
                p.setFont(f_val);
                p.drawText(pill_r, Qt::AlignCenter, pg.value_text);
                p.setFont(f_pin);
            }
        }

        // Output pins
        for (size_t pi = 0; pi < n->outputs().size() && pi < geo->outputs.size(); ++pi) {
            const Pin& pin = n->outputs()[pi];
            const PinGeometry& pg = geo->outputs[pi];
            bool conn = (!m_graph->connections_for_pin(pin.id).empty());
            QColor pcol = pin_type_color(pin.type);
            QPointF p_pos(pg.pin_x, pg.y);

            if (pin.type == PinType::Exec) {
                QPainterPath arrow;
                arrow.moveTo(p_pos.x() - 4.0f, p_pos.y() - 6.0f);
                arrow.lineTo(p_pos.x() + 4.0f, p_pos.y());
                arrow.lineTo(p_pos.x() - 4.0f, p_pos.y() + 6.0f);
                arrow.closeSubpath();
                p.setPen(QPen(pcol, 1.2f));
                p.setBrush(conn ? pcol : Qt::NoBrush);
                p.drawPath(arrow);
            } else {
                p.setPen(QPen(pcol, 1.5f));
                p.setBrush(conn ? pcol : Qt::NoBrush);
                p.drawEllipse(p_pos, 5.0f, 5.0f);
            }

            // Right-aligned rect of exactly the measured width.
            p.setPen(QColor(215, 220, 230));
            if (!pg.label_text.isEmpty()) {
                p.drawText(QRectF(pg.label_rect.x, pg.label_rect.y, pg.label_rect.w, pg.label_rect.h),
                           Qt::AlignVCenter | Qt::AlignLeft, pg.label_text);
            }
        }
    }
}

// ── Unreal Wire Slicing Laser (ConnectionDrawingPolicy.cpp & GraphEditorSettings.cpp) ──

static bool segments_intersect(const QPointF& p1, const QPointF& p2, const QPointF& p3, const QPointF& p4) {
    auto ccw = [](const QPointF& A, const QPointF& B, const QPointF& C) -> float {
        return (C.y() - A.y()) * (B.x() - A.x()) - (B.y() - A.y()) * (C.x() - A.x());
    };
    float c1 = ccw(p1, p2, p3);
    float c2 = ccw(p1, p2, p4);
    float c3 = ccw(p3, p4, p1);
    float c4 = ccw(p3, p4, p2);

    return ((c1 > 0 && c2 < 0) || (c1 < 0 && c2 > 0)) &&
           ((c3 > 0 && c4 < 0) || (c3 < 0 && c4 > 0));
}

void GraphyCanvas::draw_slice_line(QPainter& p) {
    if (m_drag_mode != DragMode::SliceWires) return;

    // Unreal GraphEditorSettings: SliceLineColor = (1.0, 0.9, 0.1), SliceLineThickness = 2.0
    QPen halo(QColor(255, 230, 25, 70), 5.0f);
    QVector<qreal> dashes;
    dashes << 6.0 << 4.0;
    halo.setDashPattern(dashes);
    p.setPen(halo);
    p.drawLine(m_slice_start, m_slice_end);

    QPen pen(QColor(255, 230, 25), 2.0f);
    pen.setDashPattern(dashes);
    p.setPen(pen);
    p.drawLine(m_slice_start, m_slice_end);
}

void GraphyCanvas::draw_marquee(QPainter& p) {
    p.setPen(QPen(QColor(240, 190, 40, 200), 1.0f, Qt::DashLine));
    p.setBrush(QColor(240, 190, 40, 30));
    p.drawRect(m_marquee_rect);
}

// ── Minimap <-> canvas mapping (set by draw_minimap) ────────────────────────

QPointF GraphyCanvas::minimap_to_canvas(const QPointF& p) const {
    if (m_minimap_scale <= 0.0f) return p;
    return QPointF((p.x() - m_minimap_origin.x()) / m_minimap_scale,
                   (p.y() - m_minimap_origin.y()) / m_minimap_scale);
}

QPointF GraphyCanvas::canvas_to_minimap(const QPointF& p) const {
    return QPointF(m_minimap_origin.x() + p.x() * m_minimap_scale,
                   m_minimap_origin.y() + p.y() * m_minimap_scale);
}

void GraphyCanvas::draw_minimap(QPainter& p) {
    if (!m_graph) return;

    const int mw = 170;
    const int mh = 115;
    const int margin = 15;
    const int inset  = 6;
    const float pad_canvas = 40.0f;

    // Do not paint a HUD bigger than the thing it summarises.
    if (width() < mw + margin * 2 || height() < mh + margin * 2) {
        m_minimap_rect = QRect();
        return;
    }

    const int mx = width() - mw - margin;
    const int my = height() - mh - margin;
    m_minimap_rect = QRect(mx, my, mw, mh);

    p.setPen(QPen(QColor(50, 54, 66), 1.0f));
    p.setBrush(QColor(18, 19, 23, 210));
    p.drawRoundedRect(m_minimap_rect, 6.0f, 6.0f);

    // Real content bounds. The previous version seeded these with 0,0 and
    // 1000,600, which forced the origin into the frame and squashed every real
    // graph into one corner of the widget regardless of where it actually was.
    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    for (const auto& n : m_graph->nodes()) {
        if (!n) continue;
        min_x = std::min(min_x, n->x());
        min_y = std::min(min_y, n->y());
        max_x = std::max(max_x, n->x() + n->width());
        max_y = std::max(max_y, n->y() + n->height());
    }
    for (const auto& c : m_graph->comments()) {
        min_x = std::min(min_x, c.x);
        min_y = std::min(min_y, c.y);
        max_x = std::max(max_x, c.x + c.width);
        max_y = std::max(max_y, c.y + c.height);
    }
    if (max_x <= min_x || max_y <= min_y) {
        min_x = 0.0f; min_y = 0.0f; max_x = 100.0f; max_y = 100.0f;
    }

    const float gw = std::max(1.0f, (max_x - min_x) + pad_canvas * 2.0f);
    const float gh = std::max(1.0f, (max_y - min_y) + pad_canvas * 2.0f);

    const QRectF inner(mx + inset, my + inset, mw - inset * 2, mh - inset * 2);

    // Uniform scale: a minimap must not distort the graph's aspect ratio.
    const float scale = std::min(inner.width() / gw, inner.height() / gh);
    const float off_x = inner.center().x() - (min_x - pad_canvas + gw * 0.5f) * scale;
    const float off_y = inner.center().y() - (min_y - pad_canvas + gh * 0.5f) * scale;

    // Publish the mapping so clicks in the HUD can be translated back into
    // canvas space (see mousePressEvent).
    m_minimap_scale  = scale;
    m_minimap_origin = QPointF(off_x, off_y);

    auto to_mini = [&](float cx, float cy) -> QPointF {
        return QPointF(off_x + cx * scale, off_y + cy * scale);
    };

    // Everything in the HUD is clipped to the HUD. Previously these rects were
    // emitted unclipped in screen space after p.restore(), so ~85 node boxes
    // bleeding across the canvas read as a stray histogram/equalizer widget.
    p.save();
    p.setClipRect(inner.toRect());

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(70, 85, 110));
    for (const auto& n : m_graph->nodes()) {
        if (!n) continue;
        QRectF r(to_mini(n->x(), n->y()),
                 to_mini(n->x() + n->width(), n->y() + n->height()));
        r = r.normalized();
        // Keep a visible floor so a node stays a mark when fully zoomed out.
        if (r.width()  < 1.0) r.setWidth(1.0);
        if (r.height() < 1.0) r.setHeight(1.0);
        p.drawRect(r);
    }

    // Comment frames, so the summary matches what the canvas actually shows.
    p.setPen(QPen(QColor(90, 100, 125, 180), 1.0f));
    p.setBrush(Qt::NoBrush);
    for (const auto& c : m_graph->comments()) {
        p.drawRect(QRectF(to_mini(c.x, c.y),
                          to_mini(c.x + c.width, c.y + c.height)).normalized());
    }

    // Viewport camera frustum in minimap
    const QRectF v_canvas = screen_to_canvas_rect(rect());
    p.setPen(QPen(QColor(240, 190, 40, 220), 1.0f));
    p.setBrush(QColor(240, 190, 40, 25));
    p.drawRect(QRectF(to_mini(v_canvas.left(), v_canvas.top()),
                      to_mini(v_canvas.right(), v_canvas.bottom())).normalized());

    p.restore();
}

// ── Mouse Events ─────────────────────────────────────────────────────────────

void GraphyCanvas::mousePressEvent(QMouseEvent* event) {
    m_last_mouse_pos = event->pos();
    QPointF cp = screen_to_canvas(event->pos());

    // Minimap interaction (Godot camera click/drag). This branch used to set
    // the drag flag and return without moving the view at all, so clicking the
    // HUD silently ate the click. It now jumps the view so the clicked point
    // becomes the centre, which is what a minimap is for.
    if (m_show_minimap && m_minimap_rect.contains(event->pos())) {
        const QPointF target = minimap_to_canvas(QPointF(event->pos()));
        m_pan_x = width()  * 0.5f - target.x() * m_zoom;
        m_pan_y = height() * 0.5f - target.y() * m_zoom;
        m_dragging_minimap = true;
        update();
        event->accept();
        return;
    }

    // Pan via Middle button
    if (event->button() == Qt::MiddleButton) {
        m_drag_mode = DragMode::Pan;
        event->accept();
        return;
    }

    // Unreal Alt+LeftClick: Sever pin connections, or Alt+Drag Wire Slicing Tool
    if (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::AltModifier)) {
        if (m_graph) {
            int hit_p = hit_test_pin(cp);
            if (hit_p > 0) {
                m_graph->disconnect_pin(hit_p);
                update();
                emit graph_modified();
                event->accept();
                return;
            }
        }
        m_drag_mode = DragMode::SliceWires;
        m_slice_start = cp;
        m_slice_end   = cp;
        update();
        event->accept();
        return;
    }

    // Left Button interactions
    if (event->button() == Qt::LeftButton) {
        // 1. Click pin -> start wire drag
        int hit_p = hit_test_pin(cp);
        if (hit_p > 0) {
            m_drag_mode = DragMode::DrawWire;
            m_wire_from_pin = hit_p;
            m_wire_cur_pos  = cp;
            m_hover_snap_pin = 0;
            update();
            event->accept();
            return;
        }

        // 2. Click connection -> select wire
        int hit_c = hit_test_connection(cp);
        if (hit_c > 0) {
            m_selected_conn_id = hit_c;
            m_selected_node_ids.clear();
            m_selected_comment_ids.clear();
            update();
            emit selection_changed();
            event->accept();
            return;
        }

        // 3. Click Node
        auto hit_n = hit_test_node(cp);
        if (hit_n) {
            m_drag_mode = DragMode::MoveNodes;
            m_drag_start_canvas = cp;
            if (!event->modifiers().testFlag(Qt::ControlModifier)) {
                if (m_selected_node_ids.count(hit_n->id()) == 0) {
                    m_selected_node_ids.clear();
                    m_selected_comment_ids.clear();
                    m_selected_node_ids.insert(hit_n->id());
                }
            } else {
                if (m_selected_node_ids.count(hit_n->id()) > 0)
                    m_selected_node_ids.erase(hit_n->id());
                else
                    m_selected_node_ids.insert(hit_n->id());
            }
            m_selected_conn_id = 0;
            update();
            emit selection_changed();
            event->accept();
            return;
        }

        // 4. Click Comment Box
        bool is_resize = false;
        int hit_comm = hit_test_comment(cp, is_resize);
        if (hit_comm > 0) {
            m_active_comment_id = hit_comm;
            m_drag_start_canvas = cp;
            for (const auto& c : m_graph->comments()) {
                if (c.id == hit_comm) {
                    m_comment_orig_rect = QRectF(c.x, c.y, c.width, c.height);
                    break;
                }
            }
            m_drag_mode = is_resize ? DragMode::ResizeComment : DragMode::MoveComment;
            if (!event->modifiers().testFlag(Qt::ControlModifier)) {
                m_selected_comment_ids.clear();
                m_selected_node_ids.clear();
            }
            m_selected_comment_ids.insert(hit_comm);
            m_selected_conn_id = 0;
            update();
            emit selection_changed();
            event->accept();
            return;
        }

        // 5. Click background -> Marquee Selection
        m_drag_mode = DragMode::Marquee;
        m_marquee_rect = QRectF(event->pos(), event->pos());
        if (!event->modifiers().testFlag(Qt::ControlModifier)) {
            clear_selection();
        }
        event->accept();
        return;
    }

    // Right Button -> Pan if dragged, or open search palette on release
    if (event->button() == Qt::RightButton) {
        m_drag_mode = DragMode::Pan;
        m_drag_start_canvas = event->pos();
        event->accept();
        return;
    }
}

void GraphyCanvas::mouseMoveEvent(QMouseEvent* event) {
    QPointF delta = event->pos() - m_last_mouse_pos;
    m_last_mouse_pos = event->pos();
    QPointF cp = screen_to_canvas(event->pos());

    // Update hover states
    m_hovered_pin = hit_test_pin(cp);
    m_hovered_conn = hit_test_connection(cp);
    m_hovered_node = hit_test_node(cp);

    if (m_drag_mode == DragMode::Pan) {
        m_pan_x += delta.x();
        m_pan_y += delta.y();
        update();
        event->accept();
        return;
    }

    if (m_drag_mode == DragMode::SliceWires) {
        m_slice_end = cp;
        update();
        event->accept();
        return;
    }

    if (m_drag_mode == DragMode::MoveNodes && m_graph) {
        float c_dx = delta.x() / m_zoom;
        float c_dy = delta.y() / m_zoom;
        for (int nid : m_selected_node_ids) {
            if (auto n = m_graph->find_node(nid)) {
                n->set_pos(n->x() + c_dx, n->y() + c_dy);
            }
        }
        update();
        emit graph_modified();
        event->accept();
        return;
    }

    if (m_drag_mode == DragMode::MoveComment && m_graph) {
        float c_dx = delta.x() / m_zoom;
        float c_dy = delta.y() / m_zoom;
        for (auto& c : m_graph->comments_mut()) {
            if (c.id == m_active_comment_id) {
                c.x += c_dx;
                c.y += c_dy;
                // Move all enclosed nodes along with it (Unreal SGraphNodeComment logic)
                for (auto& n : m_graph->nodes()) {
                    if (!n) continue;
                    if (m_comment_orig_rect.contains(n->x(), n->y())) {
                        n->set_pos(n->x() + c_dx, n->y() + c_dy);
                    }
                }
                break;
            }
        }
        m_comment_orig_rect.translate(c_dx, c_dy);
        update();
        emit graph_modified();
        event->accept();
        return;
    }

    if (m_drag_mode == DragMode::ResizeComment && m_graph) {
        float c_dx = delta.x() / m_zoom;
        float c_dy = delta.y() / m_zoom;
        for (auto& c : m_graph->comments_mut()) {
            if (c.id == m_active_comment_id) {
                c.width  = std::max(120.0f, c.width + c_dx);
                c.height = std::max(80.0f, c.height + c_dy);
                break;
            }
        }
        update();
        emit graph_modified();
        event->accept();
        return;
    }

    if (m_drag_mode == DragMode::DrawWire) {
        m_wire_cur_pos = cp;
        int target_pin = hit_test_pin(cp);
        if (target_pin > 0 && target_pin != m_wire_from_pin) {
            auto check1 = m_graph->can_connect(m_wire_from_pin, target_pin);
            auto check2 = m_graph->can_connect(target_pin, m_wire_from_pin);
            if (check1.can_connect() || check2.can_connect()) {
                m_hover_snap_pin = target_pin;
            } else {
                m_hover_snap_pin = 0;
            }
        } else {
            m_hover_snap_pin = 0;
        }
        update();
        event->accept();
        return;
    }

    if (m_drag_mode == DragMode::Marquee) {
        m_marquee_rect = QRectF(m_marquee_rect.topLeft(), event->pos()).normalized();
        QRectF canvas_box = screen_to_canvas_rect(m_marquee_rect);

        if (m_graph) {
            for (const auto& n : m_graph->nodes()) {
                if (!n) continue;
                QRectF nr(n->x(), n->y(), n->width(), n->height());
                if (canvas_box.intersects(nr)) {
                    m_selected_node_ids.insert(n->id());
                }
            }
        }
        update();
        event->accept();
        return;
    }

    update();
}

void GraphyCanvas::mouseReleaseEvent(QMouseEvent* event) {
    QPointF cp = screen_to_canvas(event->pos());
    m_dragging_minimap = false;

    // Unreal Wire Slicing: sever all connections intersected by the slice line
    if (m_drag_mode == DragMode::SliceWires && m_graph) {
        std::vector<int> sliced_conns;
        for (const auto& c : m_graph->connections()) {
            const Pin* p0 = m_graph->find_pin(c.from_pin);
            const Pin* p1 = m_graph->find_pin(c.to_pin);
            if (!p0 || !p1) continue;

            QPointF a(p0->canvas_x, p0->canvas_y);
            QPointF b(p1->canvas_x, p1->canvas_y);
            QPainterPath path = make_spline_path(a, b);

            const int steps = 24;
            QPointF prev_pt = a;
            for (int i = 1; i <= steps; ++i) {
                QPointF next_pt = path.pointAtPercent((qreal)i / steps);
                if (segments_intersect(prev_pt, next_pt, m_slice_start, m_slice_end)) {
                    sliced_conns.push_back(c.id);
                    break;
                }
                prev_pt = next_pt;
            }
        }

        for (int cid : sliced_conns) {
            m_graph->disconnect(cid);
        }

        m_drag_mode = DragMode::None;
        update();
        if (!sliced_conns.empty()) {
            emit graph_modified();
        }
        event->accept();
        return;
    }

    if (m_drag_mode == DragMode::DrawWire && m_graph) {
        int target_pin = m_hover_snap_pin > 0 ? m_hover_snap_pin : hit_test_pin(cp);
        if (target_pin > 0 && target_pin != m_wire_from_pin) {
            const Pin* p0 = m_graph->find_pin(m_wire_from_pin);
            const Pin* p1 = m_graph->find_pin(target_pin);
            if (p0 && p1) {
                if (p0->dir == PinDirection::Output)
                    m_graph->connect(p0->id, p1->id);
                else
                    m_graph->connect(p1->id, p0->id);
            }
        } else {
            // Dropped wire in empty canvas: open quick-search palette!
            show_search_palette(event->globalPosition().toPoint(), cp);
        }
        m_drag_mode = DragMode::None;
        m_wire_from_pin = 0;
        m_hover_snap_pin = 0;
        update();
        emit graph_modified();
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton && m_drag_mode == DragMode::Pan) {
        float dist = std::hypot(event->pos().x() - m_drag_start_canvas.x(),
                                event->pos().y() - m_drag_start_canvas.y());
        m_drag_mode = DragMode::None;
        if (dist < 6.0f) {
            show_search_palette(event->globalPosition().toPoint(), cp);
        }
        event->accept();
        return;
    }

    m_drag_mode = DragMode::None;
    update();
}

void GraphyCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    QPointF cp = screen_to_canvas(event->pos());

    // Double-clicking on an existing wire inserts a Reroute Knot (UK2Node_Knot)
    int hit_c = hit_test_connection(cp, 8.0f);
    if (hit_c > 0 && m_graph) {
        for (const auto& c : m_graph->connections()) {
            if (c.id == hit_c) {
                const Pin* p_out = m_graph->find_pin(c.from_pin);
                const Pin* p_in  = m_graph->find_pin(c.to_pin);
                if (p_out && p_in) {
                    PinType t = p_out->type;
                    int from_p = p_out->id;
                    int to_p   = p_in->id;

                    m_graph->disconnect(hit_c);
                    auto knot = m_graph->create_reroute_knot(t, cp.x() - 12.0f, cp.y() - 12.0f);
                    m_graph->connect(from_p, knot->inputs()[0].id);
                    m_graph->connect(knot->outputs()[0].id, to_p);

                    update();
                    emit graph_modified();
                    event->accept();
                    return;
                }
            }
        }
    }
}

void GraphyCanvas::wheelEvent(QWheelEvent* event) {
    float delta = event->angleDelta().y();
    if (delta == 0.0f) return;

    float factor = (delta > 0) ? 1.15f : (1.0f / 1.15f);
    float new_zoom = std::clamp(m_zoom * factor, 0.15f, 2.50f);

    QPointF mouse = event->position();
    m_pan_x = mouse.x() - (mouse.x() - m_pan_x) * (new_zoom / m_zoom);
    m_pan_y = mouse.y() - (mouse.y() - m_pan_y) * (new_zoom / m_zoom);
    m_zoom  = new_zoom;

    update();
    event->accept();
}

void GraphyCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F) {
        frame_all();
        event->accept();
        return;
    }

    // Minimap HUD is a preference, not a permanent overlay: several users read
    // the always-on version as a broken debug widget bleeding into the canvas.
    if (event->key() == Qt::Key_M) {
        toggle_minimap();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (!m_graph) return;
        if (m_selected_conn_id > 0) {
            m_graph->disconnect(m_selected_conn_id);
            m_selected_conn_id = 0;
        }
        for (int nid : m_selected_node_ids) {
            m_graph->remove_node(nid);
        }
        m_selected_node_ids.clear();
        for (int cid : m_selected_comment_ids) {
            m_graph->remove_comment(cid);
        }
        m_selected_comment_ids.clear();

        update();
        emit graph_modified();
        emit selection_changed();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Tab) {
        show_search_palette(mapToGlobal(rect().center()), screen_to_canvas(rect().center()));
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void GraphyCanvas::resizeEvent(QResizeEvent* /*event*/) {
    update();
}

// ── Quick Search Palette ─────────────────────────────────────────────────────

void GraphyCanvas::show_search_palette(const QPoint& screen_pos, const QPointF& canvas_pos) {
    if (!m_graph) return;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #202228; border: 1px solid #363945; color: #E0E4EE; border-radius: 6px; padding: 4px; }"
        "QMenu::item { padding: 5px 22px 5px 18px; border-radius: 4px; font-size: 11px; }"
        "QMenu::item:selected { background-color: #2F3545; color: #FFFFFF; }"
        "QMenu::separator { height: 1px; background: #323540; margin: 4px 6px; }"
    );

    // Flow Control
    QMenu* m_flow = menu.addMenu("Flow Control");
    QAction* a_branch   = m_flow->addAction("Branch (If/Else)");
    QAction* a_sequence = m_flow->addAction("Sequence");
    QAction* a_gate     = m_flow->addAction("Gate");

    // Events
    QMenu* m_events = menu.addMenu("Events");
    QAction* a_tick    = m_events->addAction("On Scene Tick");
    QAction* a_start   = m_events->addAction("On Scene Start");
    QAction* a_trigger = m_events->addAction("On Trigger Enter");

    // Math
    QMenu* m_math = menu.addMenu("Math");
    QAction* a_add   = m_math->addAction("Add (Float + Float)");
    QAction* a_sub   = m_math->addAction("Subtract (Float - Float)");
    QAction* a_mul   = m_math->addAction("Multiply (Float * Float)");
    QAction* a_cmp   = m_math->addAction("Compare (Float < Float)");
    QAction* a_clamp = m_math->addAction("Clamp");
    QAction* a_lerp  = m_math->addAction("Lerp");

    // Vector
    QMenu* m_vec = menu.addMenu("Vector");
    QAction* a_dist = m_vec->addAction("Vector Distance");
    QAction* a_vlen = m_vec->addAction("Vector Length");
    QAction* a_dot  = m_vec->addAction("Dot Product");
    QAction* a_vadd = m_vec->addAction("Vector Add");

    // Actions & Audio
    QMenu* m_actions = menu.addMenu("Actions");
    QAction* a_sfx   = m_actions->addAction("Play Sound Effect");
    QAction* a_part  = m_actions->addAction("Spawn Particles");
    QAction* a_trans = m_actions->addAction("Set Entity Transform");

    menu.addSeparator();
    QAction* a_subgraph = menu.addAction("Create Subgraph Node (Composite)");
    QAction* a_knot     = menu.addAction("Add Reroute Knot");
    QAction* a_comm     = menu.addAction("Add Comment Box (C)");

    QAction* chosen = menu.exec(screen_pos);
    if (!chosen) return;

    if (chosen == a_branch) {
        auto n = m_graph->create_node("Branch", "Flow Control", canvas_pos.x(), canvas_pos.y());
        n->set_flags(NodeFlags::Branch);
        n->add_input(m_graph->next_pin_id(), "", PinType::Exec);
        n->add_input(m_graph->next_pin_id(), "Condition", PinType::Boolean);
        n->add_output(m_graph->next_pin_id(), "True", PinType::Exec);
        n->add_output(m_graph->next_pin_id(), "False", PinType::Exec);
    } else if (chosen == a_sequence) {
        auto n = m_graph->create_node("Sequence", "Flow Control", canvas_pos.x(), canvas_pos.y());
        n->add_input(m_graph->next_pin_id(), "", PinType::Exec);
        n->add_output(m_graph->next_pin_id(), "Then 0", PinType::Exec);
        n->add_output(m_graph->next_pin_id(), "Then 1", PinType::Exec);
    } else if (chosen == a_tick) {
        auto n = m_graph->create_node("Event: On Scene Tick", "Events", canvas_pos.x(), canvas_pos.y());
        n->set_flags(NodeFlags::Event);
        n->add_output(m_graph->next_pin_id(), "", PinType::Exec);
        n->add_output(m_graph->next_pin_id(), "Delta Seconds", PinType::Float);
    } else if (chosen == a_add) {
        auto n = m_graph->create_node("Add (Float + Float)", "Math", canvas_pos.x(), canvas_pos.y());
        n->set_flags(NodeFlags::PureFunction);
        n->add_input(m_graph->next_pin_id(), "A", PinType::Float);
        n->add_input(m_graph->next_pin_id(), "B", PinType::Float, "1.0");
        n->add_output(m_graph->next_pin_id(), "Result", PinType::Float);
    } else if (chosen == a_mul) {
        auto n = m_graph->create_node("Multiply (Float * Float)", "Math", canvas_pos.x(), canvas_pos.y());
        n->set_flags(NodeFlags::PureFunction);
        n->add_input(m_graph->next_pin_id(), "A", PinType::Float);
        n->add_input(m_graph->next_pin_id(), "B", PinType::Float, "2.0");
        n->add_output(m_graph->next_pin_id(), "Result", PinType::Float);
    } else if (chosen == a_cmp) {
        auto n = m_graph->create_node("Less (Float < Float)", "Math", canvas_pos.x(), canvas_pos.y());
        n->set_flags(NodeFlags::PureFunction);
        n->add_input(m_graph->next_pin_id(), "A", PinType::Float);
        n->add_input(m_graph->next_pin_id(), "B", PinType::Float, "100.0");
        n->add_output(m_graph->next_pin_id(), "Result", PinType::Boolean);
    } else if (chosen == a_dist) {
        auto n = m_graph->create_node("Vector Distance", "Vector Math", canvas_pos.x(), canvas_pos.y());
        n->set_flags(NodeFlags::PureFunction);
        n->add_input(m_graph->next_pin_id(), "Vector A", PinType::Vector3);
        n->add_input(m_graph->next_pin_id(), "Vector B", PinType::Vector3);
        n->add_output(m_graph->next_pin_id(), "Distance", PinType::Float);
    } else if (chosen == a_sfx) {
        auto n = m_graph->create_node("Play Sound Effect", "Audio", canvas_pos.x(), canvas_pos.y());
        n->add_input(m_graph->next_pin_id(), "", PinType::Exec);
        n->add_input(m_graph->next_pin_id(), "Sound Name", PinType::String, "snd_bell_ring");
        n->add_input(m_graph->next_pin_id(), "Volume", PinType::Float, "1.0");
        n->add_output(m_graph->next_pin_id(), "", PinType::Exec);
    } else if (chosen == a_part) {
        auto n = m_graph->create_node("Spawn Particles", "VFX", canvas_pos.x(), canvas_pos.y());
        n->add_input(m_graph->next_pin_id(), "", PinType::Exec);
        n->add_input(m_graph->next_pin_id(), "Emitter Pos", PinType::Vector3);
        n->add_input(m_graph->next_pin_id(), "Preset", PinType::String, "portal_sparkles");
        n->add_output(m_graph->next_pin_id(), "", PinType::Exec);
    } else if (chosen == a_subgraph) {
        m_graph->create_subgraph_node("Custom Subroutine", canvas_pos.x(), canvas_pos.y());
    } else if (chosen == a_knot) {
        m_graph->create_reroute_knot(PinType::Exec, canvas_pos.x(), canvas_pos.y());
    } else if (chosen == a_comm) {
        m_graph->create_comment("Comment Group", canvas_pos.x(), canvas_pos.y(), 340.0f, 220.0f);
    }

    update();
    emit graph_modified();
}

} // namespace ruby::graph
