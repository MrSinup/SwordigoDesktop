#pragma once

#include <QWidget>
#include <QTimer>
#include <QVector>
#include <QPointF>
#include <QColor>
#include <QPainter>

namespace ruby::editor {

// The empty-state "studio" screen shown when no document is open. Renders a
// calm, premium backdrop: a slow aurora, drifting light motes, a faceted ruby
// emblem with an orbiting ring, an eased wordmark and a short human tagline.
class StudioIdleWidget : public QWidget {
    Q_OBJECT

public:
    explicit StudioIdleWidget(QWidget* parent = nullptr);
    ~StudioIdleWidget() override = default;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void on_animation_tick();

private:
    struct Mote {
        float x = 0.0f;        // normalized 0..1 across width
        float y = 0.0f;        // normalized 0..1 across height
        float speed = 0.0f;    // upward drift in normalized units/sec
        float size = 1.5f;     // radius in px
        float phase = 0.0f;    // sway / twinkle phase
        float alpha = 0.0f;    // base alpha 0..1
    };

    QTimer* m_timer = nullptr;
    float m_anim_time = 0.0f;
    QVector<Mote> m_motes;

    void init_motes();
    void draw_aurora(QPainter& p, int w, int h);
    void draw_motes(QPainter& p, int w, int h);
    void draw_sweep(QPainter& p, int w, int h);
    void draw_emblem(QPainter& p, const QPointF& c, float r);
    void draw_wordmark(QPainter& p, const QPointF& c, int w);
};

} // namespace ruby::editor