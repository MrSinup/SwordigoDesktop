#include "animation_control_bar.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>

namespace ruby::panels {

AnimationControlBar::AnimationControlBar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5);

    auto* play = new QPushButton(QString::fromUtf8("▶"), this);
    play->setToolTip("Play / Pause (Space)");
    connect(play, &QPushButton::clicked, this, &AnimationControlBar::toggle_playback);
    auto* stop_button = new QPushButton(QString::fromUtf8("■"), this);
    connect(stop_button, &QPushButton::clicked, this, &AnimationControlBar::stop);

    m_timeline = new QSlider(Qt::Horizontal, this);
    m_timeline->setRange(0, 0);
    connect(m_timeline, &QSlider::valueChanged, this, [this](int value) {
        m_current_frame = static_cast<float>(value);
        refresh_label();
        emit frameChanged(m_current_frame);
    });

    m_clip_box = new QComboBox(this);
    m_clip_box->setToolTip(QStringLiteral("Active Animation Clip"));
    m_clip_box->setVisible(false);
    connect(m_clip_box, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AnimationControlBar::on_clip_selected);

    m_fps = new QComboBox(this);
    m_fps->addItems({"24 FPS", "30 FPS", "60 FPS"});
    m_fps->setCurrentText("30 FPS");
    connect(m_fps, &QComboBox::currentTextChanged, this, &AnimationControlBar::update_fps);
    m_frame_label = new QLabel(this);

    layout->addWidget(play);
    layout->addWidget(stop_button);
    layout->addWidget(m_clip_box);
    layout->addWidget(m_timeline, 1);
    layout->addWidget(m_frame_label);
    layout->addWidget(m_fps);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &AnimationControlBar::advance);
    refresh_label();
}

void AnimationControlBar::set_clips(const QStringList& clip_names, int active_index) {
    QSignalBlocker blocker(m_clip_box);
    m_clip_box->clear();
    if (clip_names.isEmpty()) {
        m_clip_box->setVisible(false);
        return;
    }
    m_clip_box->addItems(clip_names);
    m_clip_box->setCurrentIndex(qBound(0, active_index, clip_names.size() - 1));
    m_clip_box->setVisible(clip_names.size() > 1);
}

void AnimationControlBar::on_clip_selected(int index) {
    if (index >= 0) {
        stop();
        emit clipChanged(index);
    }
}

void AnimationControlBar::set_frame_count(int frames) {
    m_frame_count = qMax(1, frames);
    m_timeline->setRange(0, m_frame_count - 1);
    set_frame(0.0f);
}

void AnimationControlBar::set_frame(float frame) {
    m_timeline->setValue(qBound(0, qRound(frame), m_frame_count - 1));
}

void AnimationControlBar::toggle_playback() {
    const bool playing = !m_timer->isActive();
    if (playing) m_timer->start(qMax(1, 1000 / m_playback_fps));
    else m_timer->stop();
    emit playingChanged(playing);
}

void AnimationControlBar::stop() { m_timer->stop(); set_frame(0.0f); emit playingChanged(false); }
void AnimationControlBar::advance() { set_frame(m_current_frame + 1.0f >= m_frame_count ? 0.0f : m_current_frame + 1.0f); }
void AnimationControlBar::update_fps(const QString& value) { m_playback_fps = value.left(2).toInt(); if (m_timer->isActive()) m_timer->start(1000 / m_playback_fps); }
void AnimationControlBar::refresh_label() { m_frame_label->setText(QString("Frame %1 / %2").arg(qRound(m_current_frame) + 1).arg(m_frame_count)); }

} // namespace ruby::panels
