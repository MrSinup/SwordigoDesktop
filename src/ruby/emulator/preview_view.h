// ============================================================================
// preview_view.h — zoomable "video player" viewport for the engine pod
//
// Paints the latest pod frame (letterboxed, smooth-scaled), supports zoom via
// Ctrl+wheel / buttons / pinch, pan when zoomed in, and forwards keyboard,
// mouse and wheel input to the child engine as normalized coordinates.
//
// Keyboard capture is hover-based (Android-emulator style): while the pointer
// is anywhere over the viewport and the engine runs, key events are routed to
// the game even if another Ruby widget holds Qt focus. Text fields, dropdowns
// and modal dialogs are never hijacked.
// ============================================================================
#pragma once

#include <QWidget>
#include <QImage>
#include <QSet>

class QEnterEvent;

namespace ruby::emulator {

class EnginePod;

class PreviewView : public QWidget {
    Q_OBJECT

public:
    explicit PreviewView(QWidget* parent = nullptr);
    ~PreviewView() override;

    void bind(EnginePod* pod);   // null detaches

    // ── Zoom / fit (used by the dock's control row) ─────────────────────
    int  zoom_percent() const { return m_zoom_pct; }
    void set_zoom_percent(int pct);      // 100 = fit nicely; >100 magnified
    void reset_view();                   // fit + center
    bool is_fit() const;

    // True while the cursor hovers this widget (enables key redirection).
    bool hover_active() const { return m_hover_active; }

signals:
    void zoomChanged(int pct);
    void inputFocusChanged(bool focused);

public slots:
    void on_frame(quint64 seq);        // present a new pod frame (seq-gated)
    void on_pod_state(int state);      // reset frame seq on session boundaries

protected:
    void paintEvent(QPaintEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

    // App-level key redirector (see file comment).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Rect (widget coords) the engine frame occupies, or null if none.
    bool map_to_engine(const QPointF& pos, float& nx, float& ny) const;
    void handle_key(QKeyEvent* e, bool down);
    void forward_key(int qt_key, Qt::KeyboardModifiers mods, bool down, bool repeat);
    void release_all_keys();              // flush stuck keys (focus/leave/stop)
    void release_mouse();                 // flush a stuck press (drag-abort)

    EnginePod* m_pod = nullptr;
    QImage m_frame;
    // Sequence of the frame currently on screen (monotonic per session). Any
    // incoming frame with seq <= this is stale/duplicate and is NOT presented,
    // which is what kills the "previous frame" ghosting at the widget level.
    quint64 m_display_seq = 0;
    int  m_zoom_pct = 100;   // 100 == fit nicely inside the widget
    QPointF m_pan;           // widget-space offset when zoomed in
    bool m_dragging = false;
    bool m_panning = false;
    bool m_hover_active = false;
    QPointF m_last_pos;
    // Keys currently held down toward the engine, so a keyup can always be
    // synthesized when focus leaves / the cursor leaves / the pod dies.
    QSet<int> m_held_keys;
    // Last normalized engine coords of a press — used for the matching key-up
    // even if the pointer leaves the frame before release.
    float m_last_nx = 0.0f;
    float m_last_ny = 0.0f;
    bool  m_has_press_pos = false;
    // Previous normalized touch position, for finger-motion deltas.
    float m_prev_tx = 0.0f;
    float m_prev_ty = 0.0f;
};

} // namespace ruby::emulator
