#include "scene_loading_overlay.h"
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>

namespace ruby::viewport {

SceneLoadingOverlay::SceneLoadingOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setAlignment(Qt::AlignCenter);

    m_card = new QFrame(this);
    m_card->setFixedSize(320, 160);
    m_card->setStyleSheet(
        "QFrame {"
        "  background-color: rgba(26, 28, 35, 235);"
        "  border: 1px solid rgba(220, 50, 70, 140);"
        "  border-radius: 12px;"
        "}"
    );

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(24);
    shadow->setColor(QColor(0, 0, 0, 160));
    shadow->setOffset(0, 6);
    m_card->setGraphicsEffect(shadow);

    auto* card_layout = new QVBoxLayout(m_card);
    card_layout->setContentsMargins(24, 20, 24, 20);
    card_layout->setSpacing(10);
    card_layout->setAlignment(Qt::AlignCenter);

    m_title_label = new QLabel("Loading Scene", m_card);
    m_title_label->setStyleSheet(
        "color: #ff4b68; font-size: 16px; font-weight: 700; background: transparent; border: none;"
    );
    m_title_label->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(m_title_label);

    m_stage_label = new QLabel("Parsing geometry...", m_card);
    m_stage_label->setStyleSheet(
        "color: #b0b4c0; font-size: 12px; background: transparent; border: none;"
    );
    m_stage_label->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(m_stage_label);

    m_progress = new QProgressBar(m_card);
    m_progress->setRange(0, 0); // Indeterminate animated pulse
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    m_progress->setStyleSheet(
        "QProgressBar {"
        "  background-color: rgba(45, 48, 60, 200);"
        "  border: none;"
        "  border-radius: 3px;"
        "}"
        "QProgressBar::chunk {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ff4b68, stop:1 #ff8a9e);"
        "  border-radius: 3px;"
        "}"
    );
    card_layout->addWidget(m_progress);

    root_layout->addWidget(m_card);

    m_spin_timer = new QTimer(this);
    connect(m_spin_timer, &QTimer::timeout, this, &SceneLoadingOverlay::on_spin_tick);

    hide();
}

void SceneLoadingOverlay::show_loading(const QString& title, const QString& stage) {
    m_title_label->setText(title);
    m_stage_label->setText(stage);
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
    show();
    raise();
    if (!m_spin_timer->isActive()) {
        m_spin_timer->start(30);
    }
}

void SceneLoadingOverlay::set_stage(const QString& stage) {
    m_stage_label->setText(stage);
}

void SceneLoadingOverlay::hide_loading() {
    m_spin_timer->stop();
    hide();
}

void SceneLoadingOverlay::on_spin_tick() {
    m_spinner_angle += 8.0f;
    if (m_spinner_angle >= 360.0f) m_spinner_angle -= 360.0f;
    update();
}

void SceneLoadingOverlay::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Dark translucent background scrim
    p.fillRect(rect(), QColor(10, 12, 16, 170));
}

} // namespace ruby::viewport
