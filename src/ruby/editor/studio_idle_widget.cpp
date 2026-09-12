#include "studio_idle_widget.h"

#include <QPainterPath>
#include <QRandomGenerator>
#include <QFont>
#include <QFontMetrics>
#include <QApplication>
#include <QtMath>

namespace ruby::editor {

namespace {
// Soft ease (smoothstep-ish) used to make motion feel intentional, never
// linear or janky. Returns value in [0,1] for a phase in [0,1].
float ease_in_out(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// Canned palette shared by the empty-state screen.
const QColor kBgTop(0x0c, 0x0f, 0x14);
const QColor kBgMid(0x12, 0x16, 0x1e);
const QColor kBgBot(0x0a, 0x0c, 0x11);
const QColor kRuby(0xe9, 0x45, 0x60);
const QColor kRubySoft(0xff, 0x6b, 0x8b);
const QColor kAmethyst(0x7a, 0x4f, 0xb0);
const QColor kMist(0xe8, 0xec, 0xf2);   // near-white text
const QColor kMuted(0x8b, 0x94, 0xa6);
} // namespace

StudioIdleWidget::StudioIdleWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    init_motes();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &StudioIdleWidget::on_animation_tick);
    m_timer->start(16); // ~60 FPS
}

void StudioIdleWidget::init_motes() {
    m_motes.clear();
    m_motes.reserve(34);
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < 34; ++i) {
        Mote m;
        m.x = rng->generateDouble();
        m.y = rng->generateDouble();
        m.speed = 0.010f + rng->generateDouble() * 0.022f;
        m.size = 1.0f + rng->generateDouble() * 2.4f;
        m.phase = rng->generateDouble() * 6.2832f;
        m.alpha = 0.05f + rng->generateDouble() * 0.16f;
        m_motes.push_back(m);
    }
}

void StudioIdleWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
}

void StudioIdleWidget::on_animation_tick() {
    m_anim_time += 0.016f;
    const float dt = 0.016f;
    for (auto& m : m_motes) {
        m.y -= m.speed * dt;
        if (m.y < -0.04f) {
            m.y = 1.04f;
            m.x = QRandomGenerator::global()->generateDouble();
        }
    }
    update();
}

// Deep vertical gradient with a drifting radial "aurora" so the backdrop
// feels alive rather than a flat fill.
void StudioIdleWidget::draw_aurora(QPainter& p, int w, int h) {
    QLinearGradient bg(0, 0, 0, h);
    bg.setColorAt(0.0, kBgTop);
    bg.setColorAt(0.5, kBgMid);
    bg.setColorAt(1.0, kBgBot);
    p.fillRect(0, 0, w, h, bg);

    // Two large, slowly-orbiting soft blooms in opposite phase.
    const float t = m_anim_time;
    const QPointF a(w * (0.30f + 0.22f * qSin(t * 0.11f)),
                    h * (0.34f + 0.10f * qCos(t * 0.09f)));
    const QPointF b(w * (0.70f + 0.20f * qCos(t * 0.08f + 2.0f)),
                    h * (0.58f + 0.12f * qSin(t * 0.10f)));

    const float ar = std::max(w, h) * 0.55f;
    QRadialGradient ga(a, ar);
    ga.setColorAt(0.0, QColor(kRubySoft.red(), kRubySoft.green(), kRubySoft.blue(), 22));
    ga.setColorAt(0.6, QColor(kAmethyst.red(), kAmethyst.green(), kAmethyst.blue(), 10));
    ga.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.fillRect(0, 0, w, h, ga);

    QRadialGradient gb(b, ar);
    gb.setColorAt(0.0, QColor(0x3d, 0x8b, 0xd8, 14));
    gb.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.fillRect(0, 0, w, h, gb);
}

// A slow diagonal sheen sweeps top-left -> bottom-right once per cycle.
void StudioIdleWidget::draw_sweep(QPainter& p, int w, int h) {
    const float period = 8.0f;
    const float ph = (fmodf(m_anim_time, period)) / period;   // 0..1
    const float x = -0.4f * w + ease_in_out(ph) * (w * 1.8f);

    p.save();
    QLinearGradient sweep(x - 220.0f, 0, x + 220.0f, 0);
    sweep.setColorAt(0.0, QColor(255, 255, 255, 0));
    sweep.setColorAt(0.5, QColor(255, 255, 255, 7));
    sweep.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(sweep);
    p.setPen(Qt::NoPen);
    p.translate(0, 0);
    p.rotate(-16.0f);
    p.drawRect(QRectF(x - 220.0f, -h * 0.5f, 440.0f, h * 2.0f));
    p.restore();
}

// Soft glowing specks drifting upward with a gentle sway and twinkle.
void StudioIdleWidget::draw_motes(QPainter& p, int w, int h) {
    const float t = m_anim_time;
    p.setPen(Qt::NoPen);
    for (const auto& m : m_motes) {
        const float sway = qSin(t * 0.6f + m.phase) * 18.0f;
        const float tw = 0.6f + 0.4f * qSin(t * 1.7f + m.phase * 2.0f);
        const float px = m.x * w + sway;
        const float py = m.y * h;

        const QColor core(0xff, 0xff, 0xff, int(m.alpha * tw * 255.0f));
        const QColor halo(0xcf, 0xd8, 0xe6, int(m.alpha * tw * 90.0f));
        p.setBrush(halo);
        p.drawEllipse(QPointF(px, py), m.size * 3.0f, m.size * 3.0f);
        p.setBrush(core);
        p.drawEllipse(QPointF(px, py), m.size, m.size);
    }
}

// A faceted ruby gem: crown above the girdle, pavilion below, a horizontal
// girdle facet, plus a specular glint and a soft under-glow.
void StudioIdleWidget::draw_emblem(QPainter& p, const QPointF& c, float r) {
    const float t = m_anim_time;
    const float breathe = 1.0f + 0.035f * qSin(t * 1.3f);
    const float glow_r = r * 1.6f;

    // Under-glow (pulses softly).
    QRadialGradient glow(c, glow_r);
    glow.setColorAt(0.0, QColor(kRuby.red(), kRuby.green(), kRuby.blue(), 70));
    glow.setColorAt(0.55, QColor(kRuby.red(), kRuby.green(), kRuby.blue(), 22));
    glow.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(c, glow_r, glow_r);

    p.save();
    p.translate(c);

    // Orbiting ring with a small satellite (rotates the whole ring).
    p.save();
    p.rotate(t * 22.0f);
    QPen ring(kRuby, 1.2f);
    ring.setStyle(Qt::DotLine);
    p.setPen(ring);
    p.setBrush(Qt::NoBrush);
    const float ring_r = r * 1.42f;
    p.drawEllipse(QPointF(0, 0), ring_r, ring_r);
    // Satellite
    p.setPen(Qt::NoPen);
    p.setBrush(kRubySoft);
    p.drawEllipse(QPointF(ring_r, 0), 2.2f, 2.2f);
    p.restore();

    // A faint counter-rotating circle for parallax depth.
    p.save();
    p.rotate(-t * 9.0f);
    QPen ring2(QColor(0x3d, 0x8b, 0xd8, 70), 1.0f);
    ring2.setStyle(Qt::SolidLine);
    p.setPen(ring2);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(0, 0), r * 1.72f, r * 1.72f);
    p.restore();

    // Gem geometry.
    const float gr = r * breathe;
    const QPointF top(0, -gr * 0.82f);
    const QPointF left(-gr, 0);
    const QPointF right(gr, 0);
    const QPointF bottom(0, gr * 0.86f);

    QPainterPath gem;
    gem.moveTo(top);
    gem.lineTo(left);
    gem.lineTo(bottom);
    gem.lineTo(right);
    gem.closeSubpath();

    QLinearGradient fill(-gr, -gr, gr, gr);
    fill.setColorAt(0.0, QColor(0xff, 0x8a, 0xa4));
    fill.setColorAt(0.45, kRubySoft);
    fill.setColorAt(1.0, QColor(0x8e, 0x2f, 0x4d));

    p.setPen(QPen(QColor(0xff, 0xff, 0xff, 150), 1.2f));
    p.setBrush(fill);
    p.drawPath(gem);

    // Girdle facet (horizontal) + crown/pavilion facet seams.
    p.setPen(QPen(QColor(0xff, 0xff, 0xff, 60), 0.8f));
    p.drawLine(left, right);
    p.drawLine(top, QPointF(0, 0));
    p.drawLine(bottom, QPointF(0, 0));
    // Crown edge seams toward the top point.
    p.drawLine(left, top);
    p.drawLine(right, top);

    // Specular glint (upper-left facet catch-light).
    p.setPen(Qt::NoPen);
    QRadialGradient glint(QPointF(-gr * 0.3f, -gr * 0.42f), gr * 0.5f);
    glint.setColorAt(0.0, QColor(255, 255, 255, 90));
    glint.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(glint);
    p.drawEllipse(QPointF(-gr * 0.3f, -gr * 0.42f), gr * 0.5f, gr * 0.5f);

    p.restore();
}

void StudioIdleWidget::draw_wordmark(QPainter& p, const QPointF& c, int w) {
    const QFont base_font = QApplication::font();

    // "RUBY" — primary mark with generous tracking.
    QFont title = base_font;
    title.setPointSizeF(30.0f);
    title.setBold(true);
    title.setLetterSpacing(QFont::AbsoluteSpacing, 9.0f);
    QFontMetrics tm(title);
    QRectF title_rect(c.x() - w, c.y(), w * 2, tm.height());
    p.setFont(title);
    p.setPen(kMist);
    p.drawText(title_rect, Qt::AlignHCenter | Qt::AlignTop, "RUBY");

    // "STUDIO" — spaced secondary caption.
    QFont sub = base_font;
    sub.setPointSizeF(11.5f);
    sub.setBold(false);
    sub.setLetterSpacing(QFont::AbsoluteSpacing, 12.0f);
    QFontMetrics sm(sub);
    QRectF sub_rect(c.x() - w, c.y() + tm.height() - 2.0f, w * 2, sm.height());
    p.setFont(sub);
    p.setPen(kMuted);
    p.drawText(sub_rect, Qt::AlignHCenter | Qt::AlignTop, "STUDIO");
}

void StudioIdleWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0) return;

    draw_aurora(p, w, h);
    draw_sweep(p, w, h);
    draw_motes(p, w, h);

    const QPointF center(w * 0.5f, h * 0.40f);
    draw_emblem(p, center, 54.0f);

    const QFont base_font = QApplication::font();

    // Wordmark, gently eased upward as it settles in.
    const float settle = ease_in_out(fminf(m_anim_time / 0.9f, 1.0f));
    const QPointF mark_center(center.x(), center.y() + 92.0f - settle * 10.0f);
    draw_wordmark(p, mark_center, w);

    // One-line hint, kept minimal and at the bottom.
    QFont hint = base_font;
    hint.setPointSizeF(11.5f);
    p.setFont(hint);
    p.setPen(QColor(0x6c, 0x75, 0x86, 200));
    QRectF hint_rect(center.x() - w, h - 46.0f, w * 2, 20);
    p.drawText(hint_rect, Qt::AlignHCenter, "Open a scene, script, or model to begin");
}

} // namespace ruby::editor