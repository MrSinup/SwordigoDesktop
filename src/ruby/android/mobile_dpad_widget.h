#pragma once
// ============================================================================
// mobile_dpad_widget.h — Touch Virtual D-Pad & Camera Joystick HUD
//   Modular on-screen navigation controller for Ruby GG Mobile Viewport.
//   Supports 4-way direction pad, continuous analog vector tracking,
//   and Pan vs Dolly/Fly mode toggling.
// ============================================================================

#include <QWidget>
#include <QPointF>
#include <QColor>
#include <QTimer>

namespace ruby::android {

class MobileDPadWidget : public QWidget {
    Q_OBJECT

public:
    enum class NavMode {
        Pan,        // Move camera target along camera-local X/Y plane
        Dolly       // Move camera along gaze vector (forward/back) and strafe
    };

    explicit MobileDPadWidget(QWidget* parent = nullptr);
    ~MobileDPadWidget() override = default;

    NavMode nav_mode() const { return m_mode; }
    void set_nav_mode(NavMode mode);

    // Dynamic opacity (translucent when idle, more opaque on touch)
    void set_base_opacity(qreal opacity);

signals:
    // Dispatched continuously at 60 FPS while user is pressing the D-Pad
    // dx, dy are in normalized range [-1.0, 1.0]
    void panRequested(float dx, float dy);
    void dollyRequested(float forward, float strafe);
    void modeChanged(NavMode new_mode);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    QSize sizeHint() const override;

private slots:
    void on_dispatch_tick();

private:
    void update_vector_from_pos(const QPointF& pos);
    void reset_vector();

    NavMode m_mode = NavMode::Pan;
    bool m_is_pressed = false;
    QPointF m_center;
    QPointF m_knob_pos;
    QPointF m_normalized_vec; // x: [-1..1], y: [-1..1]
    float m_outer_radius = 65.0f;
    float m_dead_zone = 10.0f;
    float m_knob_radius = 24.0f;
    qreal m_base_opacity = 0.55;
    qreal m_active_opacity = 0.88;

    QTimer m_tick_timer;
};

} // namespace ruby::android
