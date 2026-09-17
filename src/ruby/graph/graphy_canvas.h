#pragma once
// ============================================================================
// graphy_canvas.h — Unreal-Style Node Graph Canvas Widget for Ruby GG
// Features:
//   - Infinite smooth pan & zoom canvas with Blueprint dot-matrix grid
//   - Rounded node cards with category-specific gradient header banners
//   - Circular typed pins with inner hole / filled connection state
//   - Directional cubic Bézier spline cables with glowing hover halo
//   - Interactive wire dragging with magnetic pin snapping
//   - Reroute knot pins created on double-click wire
//   - Resizable comment / group boxes that carry enclosed nodes
//   - Quick-Search context palette (Tab / Right-Click / Cable Drop)
//   - Live Minimap overlay with viewport navigation
//   - Marquee multi-selection, duplication, copy/paste, undo support
// ============================================================================

#include "graphy.h"
#include "graphy_layout.h"
#include <QWidget>
#include <QPointF>
#include <QRectF>
#include <QPainterPath>
#include <unordered_map>
#include <unordered_set>

class QMenu;
class QLineEdit;

namespace ruby::graph {

class GraphyCanvas : public QWidget {
    Q_OBJECT
public:
    explicit GraphyCanvas(QWidget* parent = nullptr);
    ~GraphyCanvas() override = default;

    void set_graph(std::shared_ptr<Graph> g);
    std::shared_ptr<Graph> graph() const { return m_graph; }

    /// Center view on all nodes
    void frame_all();

    /// Clear selection
    void clear_selection();

    // ── Layout ───────────────────────────────────────────────────────────────
    // Geometry is computed by `graphy_layout.cpp` and cached here, so the
    // painter and hit-testing can never disagree about where a rect is.
    // Changing metrics re-measures immediately (no rebuild needed to retune).

    const LayoutMetrics& metrics() const { return m_metrics; }
    void set_metrics(const LayoutMetrics& m);

    /// Text measurer used for every rect. Defaults to font metrics; overriding
    /// it makes the canvas deterministic for tests and for embedders that want
    /// a different font.
    const TextMeasure& text_measure() const { return m_measure; }
    void set_text_measure(TextMeasure measure);

    /// Cached geometry for a node, or nullptr when the node is unknown.
    const NodeGeometry* node_geometry(int node_id) const;

    // ── Minimap HUD ──────────────────────────────────────────────────────────
    bool minimap_visible() const { return m_show_minimap; }
    void set_minimap_visible(bool visible);

public slots:
    /// Toggle the minimap HUD (also bound to the M key).
    void toggle_minimap();

signals:
    void selection_changed();
    void graph_modified();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // Coordinate transformations
    QPointF screen_to_canvas(const QPointF& sp) const;
    QPointF canvas_to_screen(const QPointF& cp) const;
    QRectF  screen_to_canvas_rect(const QRectF& sr) const;
    QRectF  canvas_to_screen_rect(const QRectF& cr) const;

    // Hit testing
    int  hit_test_pin(const QPointF& canvas_pt) const;
    std::shared_ptr<Node> hit_test_node(const QPointF& canvas_pt) const;
    int  hit_test_connection(const QPointF& canvas_pt, float tolerance = 7.0f) const;
    int  hit_test_comment(const QPointF& canvas_pt, bool& out_resize_handle) const;

    // Layout & geometry cache
    void update_node_layout(Node& node);
    void relayout_all();
    QPainterPath make_spline_path(const QPointF& p0, const QPointF& p1) const;

    // Painting sub-passes
    void draw_grid(QPainter& p);
    void draw_comments(QPainter& p);
    void draw_connections(QPainter& p);
    void draw_active_wire(QPainter& p);
    void draw_nodes(QPainter& p);
    void draw_slice_line(QPainter& p);
    void draw_marquee(QPainter& p);
    void draw_minimap(QPainter& p);

    // Quick Search Palette
    void show_search_palette(const QPoint& screen_pos, const QPointF& canvas_pos);

private:
    std::shared_ptr<Graph> m_graph;

    // Navigation (Pan & Zoom)
    float m_pan_x = 100.0f;
    float m_pan_y = 100.0f;
    float m_zoom  = 1.0f;

    // Interaction state
    enum class DragMode {
        None,
        Pan,
        MoveNodes,
        MoveComment,
        ResizeComment,
        DrawWire,
        Marquee,
        SliceWires
    };
    DragMode m_drag_mode = DragMode::None;

    QPointF m_last_mouse_pos;
    QPointF m_drag_start_canvas;
    QRectF  m_marquee_rect;

    // Wire Slicing (Unreal Alt+Drag laser cutter)
    QPointF m_slice_start;
    QPointF m_slice_end;

    // Selection
    std::unordered_set<int> m_selected_node_ids;
    std::unordered_set<int> m_selected_comment_ids;
    int m_selected_conn_id = 0;

    // Comment dragging / resizing
    int m_active_comment_id = 0;
    QRectF m_comment_orig_rect;

    // Live Wire Dragging
    int     m_wire_from_pin = 0;
    QPointF m_wire_cur_pos;
    int     m_hover_snap_pin = 0;

    // Hover state
    int m_hovered_pin = 0;
    int m_hovered_conn = 0;
    std::shared_ptr<Node> m_hovered_node;

    // Geometry cache, refreshed by relayout_all() at the top of paintEvent.
    LayoutMetrics m_metrics;
    TextMeasure   m_measure;
    std::unordered_map<int, NodeGeometry> m_geometry;

    // Minimap
    bool m_show_minimap = true;
    QRect m_minimap_rect;
    bool m_dragging_minimap = false;

    // Minimap -> canvas mapping, published by draw_minimap() so that clicks in
    // the HUD navigate the view instead of being swallowed.
    float   m_minimap_scale  = 1.0f;
    QPointF m_minimap_origin;
    QPointF minimap_to_canvas(const QPointF& p) const;
    QPointF canvas_to_minimap(const QPointF& p) const;
};

} // namespace ruby::graph
