#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QSlider;
class QTimer;

namespace ruby::panels {

// A transport widget deliberately independent from the viewport so it can be
// docked, floated, or reused by future scene and animation editors.
class AnimationControlBar final : public QWidget {
    Q_OBJECT

public:
    explicit AnimationControlBar(QWidget* parent = nullptr);
    void set_frame_count(int frames);
    void set_frame(float frame);
    void set_clips(const QStringList& clip_names, int active_index = 0);

signals:
    void frameChanged(float frame);
    void playingChanged(bool playing);
    void clipChanged(int clip_index);

private slots:
    void toggle_playback();
    void stop();
    void advance();
    void update_fps(const QString& value);
    void on_clip_selected(int index);

private:
    void refresh_label();

    QSlider* m_timeline = nullptr;
    QComboBox* m_clip_box = nullptr;
    QComboBox* m_fps = nullptr;
    QLabel* m_frame_label = nullptr;
    QTimer* m_timer = nullptr;
    int m_frame_count = 1;
    float m_current_frame = 0.0f;
    int m_playback_fps = 30;
};

} // namespace ruby::panels
