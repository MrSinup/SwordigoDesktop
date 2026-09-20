// ============================================================================
// mobile_dpad_widget.cpp — Implementation of Touch Virtual D-Pad HUD
// ============================================================================

#include "mobile_dpad_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>

namespace ruby::android {

MobileDPadWidget::MobileDPadWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFixedSize(160, 160);

    m_center = QPointF(width() / 2.0, height() / 2.0);
    m_knob_pos = m_center;
    m_normalized_vec = QPointF(0.0, 0.0);

    connect(&m_tick_timer, &QTimer::timeout, this, &MobileDPadWidget::on_dispatch_tick);
}

void MobileDPadWidget::set_nav_mode(NavMode mode) {
    if (m_mode != mode) {
        m_mode = mode;
        emit modeChanged(m_mode);
        update();
    }
}

void MobileDPadWidget::set_base_opacity(qreal opacity) {
    m_base_opacity = opacity;
    update();
}

QSize MobileDPadWidget::sizeHint() const {
    return QSize(160, 160);
}

void MobileDPadWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal current_opacity = m_is_pressed ? m_active_opacity : m_base_opacity;
    p.setOpacity(current_opacity);

    m_center = QPointF(width() / 2.0, height() / 2.0);

    // 1. Draw outer base ring (Dark glassmorphism style)
    QRadialGradient bg_grad(m_center, m_outer_radius);
    bg_grad.setColorAt(0.0, QColor(25, 28, 35, 180));
    bg_grad.setColorAt(0.85, QColor(15, 17, 22, 220));
    bg_grad.setColorAt(1.0, QColor(45, 50, 65, 255));

    p.setPen(QPen(QColor(80, 95, 125, 180), 2.0));
    p.setBrush(bg_grad);
    p.drawEllipse(m_center, m_outer_radius, m_outer_radius);

    // 2. Draw cross direction guides
    p.setPen(QPen(QColor(120, 140, 180, 70), 1.5, Qt::DashLine));
    p.drawLine(QPointF(m_center.x() - m_outer_radius * 0.75, m_center.y()),
               QPointF(m_center.x() + m_outer_radius * 0.75, m_center.y()));
    p.drawLine(QPointF(m_center.x(), m_center.y() - m_outer_radius * 0.75),
               QPointF(m_center.x(), m_center.y() + m_outer_radius * 0.75));

    // 3. Draw direction arrow hints
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(160, 185, 230, m_is_pressed ? 180 : 110));

    // Top arrow
    QPolygonF top_arrow;
    top_arrow << QPointF(m_center.x(), m_center.y() - m_outer_radius + 6.0)
              << QPointF(m_center.x() - 6.0, m_center.y() - m_outer_radius + 15.0)
              << QPointF(m_center.x() + 6.0, m_center.y() - m_outer_radius + 15.0);
    p.drawPolygon(top_arrow);

    // Bottom arrow
    QPolygonF bottom_arrow;
    bottom_arrow << QPointF(m_center.x(), m_center.y() + m_outer_radius - 6.0)
                 << QPointF(m_center.x() - 6.0, m_center.y() + m_outer_radius - 15.0)
                 << QPointF(m_center.x() + 6.0, m_center.y() + m_outer_radius - 15.0);
    p.drawPolygon(bottom_arrow);

    // Left arrow
    QPolygonF left_arrow;
    left_arrow << QPointF(m_center.x() - m_outer_radius + 6.0, m_center.y())
               << QPointF(m_center.x() - m_outer_radius + 15.0, m_center.y() - 6.0)
               << QPointF(m_center.x() - m_outer_radius + 15.0, m_center.y() + 6.0);
    p.drawPolygon(left_arrow);

    // Right arrow
    QPolygonF right_arrow;
    right_arrow << QPointF(m_center.x() + m_outer_radius - 6.0, m_center.y())
                << QPointF(m_center.x() + m_outer_radius - 15.0, m_center.y() - 6.0)
                << QPointF(m_center.x() + m_outer_radius - 15.0, m_center.y() + 6.0);
    p.drawPolygon(right_arrow);

    // 4. Draw Center Knob (Thumb indicator)
    QRadialGradient knob_grad(m_knob_pos, m_knob_radius);
    if (m_mode == NavMode::Pan) {
        // Cyan / Blue tint for Pan Mode
        knob_grad.setColorAt(0.0, QColor(70, 160, 245, 230));
        knob_grad.setColorAt(0.8, QColor(25, 90, 180, 240));
        knob_grad.setColorAt(1.0, QColor(15, 50, 120, 255));
        p.setPen(QPen(QColor(140, 210, 255, 220), 2.0));
    } else {
        // Emerald / Amber tint for Dolly/Fly Mode
        knob_grad.setColorAt(0.0, QColor(245, 170, 70, 230));
        knob_grad.setColorAt(0.8, QColor(190, 110, 20, 240));
        knob_grad.setColorAt(1.0, QColor(130, 70, 10, 255));
        p.setPen(QPen(QColor(255, 215, 140, 220), 2.0));
    }

    p.setBrush(knob_grad);
    p.drawEllipse(m_knob_pos, m_knob_radius, m_knob_radius);

    // Mode label inside knob
    p.setPen(QColor(255, 255, 255, 240));
    QFont font = p.font();
    font.setPointSize(8);
    font.setBold(true);
    p.setFont(font);
    const QString mode_text = (m_mode == NavMode::Pan) ? QStringLiteral("PAN") : QStringLiteral("FLY");
    p.drawText(QRectF(m_knob_pos.x() - m_knob_radius, m_knob_pos.y() - m_knob_radius,
                      m_knob_radius * 2.0, m_knob_radius * 2.0),
               Qt::AlignCenter, mode_text);
}

void MobileDPadWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_is_pressed = true;
        update_vector_from_pos(event->position());
        m_tick_timer.start(16); // ~60 FPS
        update();
    }
}

void MobileDPadWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_is_pressed) {
        update_vector_from_pos(event->position());
        update();
    }
}

void MobileDPadWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Check if released near the center knob without dragging -> Mode Toggle!
        const float dist_from_center = std::hypot(event->position().x() - m_center.x(),
                                                  event->position().y() - m_center.y());
        if (dist_from_center < m_knob_radius * 0.9f && std::hypot(m_normalized_vec.x(), m_normalized_vec.y()) < 0.15f) {
            set_nav_mode(m_mode == NavMode::Pan ? NavMode::Dolly : NavMode::Pan);
        }

        m_is_pressed = false;
        m_tick_timer.stop();
        reset_vector();
        update();
    }
}

void MobileDPadWidget::update_vector_from_pos(const QPointF& pos) {
    m_center = QPointF(width() / 2.0, height() / 2.0);
    const float dx = static_cast<float>(pos.x() - m_center.x());
    const float dy = static_cast<float>(pos.y() - m_center.y());
    const float dist = std::hypot(dx, dy);

    if (dist <= m_dead_zone) {
        m_knob_pos = m_center;
        m_normalized_vec = QPointF(0.0, 0.0);
        return;
    }

    const float max_dist = m_outer_radius - m_knob_radius * 0.5f;
    const float clamped_dist = std::min(dist, max_dist);
    const float dir_x = dx / dist;
    const float dir_y = dy / dist;

    m_knob_pos = QPointF(m_center.x() + dir_x * clamped_dist,
                         m_center.y() + dir_y * clamped_dist);

    // Normalized intensity from 0.0 to 1.0 (with dead-zone compensation)
    const float intensity = (clamped_dist - m_dead_zone) / (max_dist - m_dead_zone);
    m_normalized_vec = QPointF(dir_x * intensity, dir_y * intensity);
}

void MobileDPadWidget::reset_vector() {
    m_center = QPointF(width() / 2.0, height() / 2.0);
    m_knob_pos = m_center;
    m_normalized_vec = QPointF(0.0, 0.0);
}

void MobileDPadWidget::on_dispatch_tick() {
    if (!m_is_pressed) return;

    const float vx = static_cast<float>(m_normalized_vec.x());
    const float vy = static_cast<float>(m_normalized_vec.y());

    if (std::abs(vx) < 1e-4f && std::abs(vy) < 1e-4f) return;

    if (m_mode == NavMode::Pan) {
        // vx -> camera right/left pan, vy -> camera down/up pan (inverted screen y)
        emit panRequested(vx, -vy);
    } else {
        // vy -> forward/backward (dolly), vx -> strafe
        emit dollyRequested(-vy, vx);
    }
}

} // namespace ruby::android
