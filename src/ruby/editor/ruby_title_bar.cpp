// ============================================================================
// ruby_title_bar.cpp — Unified Custom Studio Title Bar Implementation
// ============================================================================

#include "ruby_title_bar.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QWindow>
#include <algorithm>

namespace ruby::editor {

namespace {
// Same gray the QSS paints window-control text with; painted icons use this
// so they stay consistent with the theme on every platform.
constexpr QColor kControlColor(0x8b, 0x94, 0x9e);
constexpr int kIconLogical = 16;   // logical icon size (px)
} // namespace

QIcon RubyTitleBar::make_control_icon(IconKind kind) {
    // Paint at 2× device-pixel ratio so HiDPI / Wayland fractional scaling
    // keeps the 1.6 px strokes crisp instead of blurry.
    const qreal dpr = 2.0;
    QPixmap pm(int(kIconLogical * dpr), int(kIconLogical * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(kControlColor, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal s = 3.0;                 // margin (logical px)
    const qreal w = (qreal)kIconLogical; // logical extent
    switch (kind) {
        case IconMinimize:
            p.drawLine(QPointF(s + 1.0, w / 2.0),
                       QPointF(w - s - 1.0, w / 2.0));
            break;
        case IconMaximize:
            p.drawRect(QRectF(s, s + 1.0, w - 2.0 * s, w - 2.0 * s - 1.0));
            break;
        case IconRestore: {
            // Back square, then the front square offset down-right.
            p.drawRect(QRectF(s + 2.5, s - 0.5, w - 2.0 * s - 2.5, w - 2.0 * s - 2.5));
            p.drawRect(QRectF(s - 0.5, s + 2.5, w - 2.0 * s - 2.5, w - 2.0 * s - 2.5));
            break;
        }
        case IconClose:
            p.drawLine(QPointF(s, s), QPointF(w - s, w - s));
            p.drawLine(QPointF(w - s, s), QPointF(s, w - s));
            break;
    }
    return QIcon(pm);
}

RubyTitleBar::RubyTitleBar(QWidget* parent, QMenuBar* menu_bar) : QWidget(parent) {
    setObjectName("rubyTitleBar");
    setFixedHeight(36);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 0, 0); // 0 right margin so window controls hug the edge
    layout->setSpacing(8);

    // 1. Ruby Brand Mark + Title
    auto* mark = new QLabel(QString::fromUtf8("◆"), this);
    mark->setObjectName("rubyMark");
    mark->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto* name = new QLabel("Ruby Studio", this);
    name->setObjectName("rubyTitle");
    name->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto* sep = new QLabel("|", this);
    sep->setObjectName("rubyTitleSep");
    sep->setStyleSheet("color: #3b4048; font-size: 13px; font-weight: 300; margin: 0 4px;");
    sep->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    layout->addWidget(mark);
    layout->addWidget(name);
    layout->addWidget(sep);

    // 2. Menu Bar (File, View, Tools, Help)
    if (menu_bar) {
        menu_bar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        menu_bar->setStyleSheet("QMenuBar { background: transparent; border: none; }");
        layout->addWidget(menu_bar);
    }

    // 3. Wide draggable spacer
    layout->addStretch(1);

    // 4. Window Controls on the FAR RIGHT — painted vector icons (see
    //    make_control_icon): font glyphs for these symbols render as faint
    //    broken strokes on several Linux font stacks, making the Close button
    //    effectively invisible (frameless window → no WM decorations either).
    auto make_control = [this, layout](IconKind icon, const QString& tip,
                                       const QString& objName) {
        auto* b = new QToolButton(this);
        b->setIcon(make_control_icon(icon));
        b->setIconSize(QSize(kIconLogical, kIconLogical));
        b->setToolTip(tip);
        b->setObjectName(objName);
        b->setFixedSize(46, 36);
        b->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(b);
        return b;
    };

    m_min_btn   = make_control(IconMinimize, "Minimize", "windowControl");
    m_max_btn   = make_control(IconMaximize, "Maximize / Restore", "windowControl");
    m_close_btn = make_control(IconClose, "Close", "windowClose");

    connect(m_min_btn, &QToolButton::clicked, this, [this] {
        if (window()) window()->showMinimized();
    });

    connect(m_max_btn, &QToolButton::clicked, this, [this] {
        if (!window()) return;
        if (window()->isMaximized()) {
            window()->showNormal();
        } else {
            window()->showMaximized();
        }
        update_maximize_button();
    });

    connect(m_close_btn, &QToolButton::clicked, this, [this] {
        if (window()) window()->close();
    });
}

void RubyTitleBar::update_maximize_button() {
    if (!m_max_btn || !window()) return;
    m_max_btn->setIcon(make_control_icon(window()->isMaximized()
                                             ? IconRestore
                                             : IconMaximize));
}

void RubyTitleBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !window()) return;

    // Dragging a maximized frameless window: restore first so the system move
    // grabs the restored frame (matches native title-bar behavior on Windows /
    // KDE / GNOME).
    if (window()->isMaximized()) {
        window()->showNormal();
        update_maximize_button();
    }

    // Prefer the native interactive move (works on Wayland AND X11; on
    // Wayland a plain window()->move() is a no-op by design, so this is the
    // only reliable way to drag a frameless window under KWin / Mutter).
    if (window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        return;   // system move owns the drag; no manual fallback needed
    }

    // No native window handle (e.g. offscreen platform): manual fallback.
    m_dragging = true;
    m_drag_offset = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
}

void RubyTitleBar::mouseMoveEvent(QMouseEvent* event) {
    // Only the no-native-handle fallback path reaches here; when a system
    // move is active the compositor drives the drag and we must NOT also call
    // window()->move() (it fights the system move on X11 and is ignored on
    // Wayland).
    if (m_dragging && (event->buttons() & Qt::LeftButton) && window()) {
        window()->move(event->globalPosition().toPoint() - m_drag_offset);
    }
}

void RubyTitleBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
    }
}

void RubyTitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && window()) {
        if (window()->isMaximized()) {
            window()->showNormal();
        } else {
            window()->showMaximized();
        }
        update_maximize_button();
    }
}

} // namespace ruby::editor