// ============================================================================
// preview_view.cpp — see preview_view.h
// ============================================================================
#include "ruby/emulator/preview_view.h"
#include "ruby/emulator/engine_pod.h"

#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>
#include <QApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QCursor>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_keyboard.h>

namespace ruby::emulator {

// Defined below (Input forwarding section) — declared here because the
// stuck-key helpers above need them.
static unsigned qt_to_sdl_key(int qt_key);
static unsigned qt_to_sdl_mods(Qt::KeyboardModifiers mods);

PreviewView::PreviewView(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(240, 160);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    // Redirect keyboard to the game while the cursor simply hovers the
    // emulator (Android-emulator / game-window behaviour — no focus lock).
    if (QApplication::instance()) QApplication::instance()->installEventFilter(this);
}

PreviewView::~PreviewView() {
    if (QApplication::instance()) QApplication::instance()->removeEventFilter(this);
}

void PreviewView::bind(EnginePod* pod) {
    m_pod = pod;
    if (m_pod) {
        connect(m_pod, &EnginePod::frameAvailable, this, &PreviewView::on_frame,
                Qt::QueuedConnection);
        connect(m_pod, &EnginePod::stateChanged,
                this, &PreviewView::on_pod_state, Qt::QueuedConnection);
    }
    m_display_seq = 0;
    m_frame = QImage();
    reset_view();
    update();
}

void PreviewView::on_pod_state(int state) {
    // A fresh session restarts the pod's frame sequence from zero — forget the
    // old watermark so the first new frame is accepted again.
    if (state == EnginePod::kBooting || state == EnginePod::kStopped) {
        m_display_seq = 0;
        if (state == EnginePod::kStopped) {
            m_frame = QImage();   // don't keep a corpse frame on screen
        }
        update();
    }
}

void PreviewView::on_frame(quint64 seq) {
    if (!m_pod) return;
    // Strictly-monotonic presentation: reject stale/duplicate frames so the
    // viewer never jumps back to an older frame (ring grabs are newest-first;
    // this guards the widget against any residual reordering).
    if (seq == 0 || seq <= m_display_seq) return;
    m_display_seq = seq;
    // Deep-copy: the pod reuses its QImage buffer on every grab.
    m_frame = m_pod->frame().copy();
    update();
}

// ---------------------------------------------------------------------------
// Zoom / fit
// ---------------------------------------------------------------------------
static float fit_scale(const QSize& view, const QSize& frame) {
    if (frame.isEmpty()) return 1.0f;
    float sx = (float)view.width() / frame.width();
    float sy = (float)view.height() / frame.height();
    return qMin(sx, sy);
}

void PreviewView::set_zoom_percent(int pct) {
    pct = qBound(25, pct, 600);
    if (pct == m_zoom_pct) return;
    m_zoom_pct = pct;
    emit zoomChanged(pct);
    update();
}

bool PreviewView::is_fit() const { return m_zoom_pct <= 100; }

void PreviewView::reset_view() {
    m_zoom_pct = 100;
    m_pan = QPointF();
    emit zoomChanged(m_zoom_pct);
    update();
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void PreviewView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(8, 9, 11));

    if (m_frame.isNull()) {
        p.setPen(QColor(90, 98, 110));
        QFont f = p.font();
        f.setPointSizeF(9.5);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   m_pod && m_pod->state() == EnginePod::kBooting
                       ? "Booting engine…\n(1.4.13 + libsre13)"
                       : "Engine stopped.\nPress ▶ Boot to run Swordigo 1.4.13");
        return;
    }

    float fit = fit_scale(size(), m_frame.size());
    float scale = fit * m_zoom_pct / 100.0f;
    QSizeF disp((float)m_frame.width() * scale, (float)m_frame.height() * scale);
    QRectF target((width() - disp.width()) / 2.0 + m_pan.x(),
                  (height() - disp.height()) / 2.0 + m_pan.y(),
                  disp.width(), disp.height());
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom_pct <= 150);
    p.drawImage(target, m_frame);
    if (m_zoom_pct > 100) {
        p.setPen(QPen(QColor(255, 255, 255, 26), 1));
        p.drawRect(target.adjusted(0, 0, -1, -1));
    }
}

// Key pump: a single down/up forwarder with stuck-key protection. Qt delivers
// auto-repeat KeyPress events while a key is held; only the FIRST down and the
// final up should reach the engine's state machine.
void PreviewView::handle_key(QKeyEvent* e, bool down) {
    if (!m_pod || !m_pod->is_alive()) {
        m_held_keys.clear();
        return;
    }
    const int k = e->key();
    if (k == 0 || k == Qt::Key_unknown) return;
    const bool repeat = e->isAutoRepeat();
    if (down) {
        if (!repeat) {
            // First down edge: track + forward exactly one down.
            m_held_keys.insert(k);
            forward_key(k, e->modifiers(), true, false);
        } else if (!m_held_keys.contains(k)) {
            // The original press was lost (focus switched mid-hold) — treat
            // this repeat as the down edge.
            m_held_keys.insert(k);
            forward_key(k, e->modifiers(), true, false);
        }
        // Repeated downs are intentionally NOT forwarded again.
    } else {
        // Real key-up: forward an UP whenever the key was actually held.
        // (Some backends flag even the final release as "auto-repeat" — e.g.
        // X11 — so gate on held-state, not on the repeat flag, otherwise the
        // engine never sees the release and the button stays stuck.)
        if (m_held_keys.remove(k)) {
            forward_key(k, e->modifiers(), false, false);
        }
    }
}

void PreviewView::forward_key(int qt_key, Qt::KeyboardModifiers mods, bool down, bool repeat) {
    if (!m_pod || !m_pod->is_alive()) return;
    unsigned key = qt_to_sdl_key(qt_key);
    if (!key) return;
    unsigned sc = (unsigned)SDL_GetScancodeFromKey((SDL_Keycode)key, nullptr);
    m_pod->send_key(key, sc, qt_to_sdl_mods(mods), down, repeat);
}

void PreviewView::release_all_keys() {
    if (m_held_keys.isEmpty()) return;
    if (m_pod && m_pod->is_alive()) {
        for (int k : m_held_keys) {
            unsigned key = qt_to_sdl_key(k);
            if (!key) continue;
            unsigned sc = (unsigned)SDL_GetScancodeFromKey((SDL_Keycode)key, nullptr);
            m_pod->send_key(key, sc, 0, false, false);
        }
    }
    m_held_keys.clear();
}

void PreviewView::release_mouse() {
    if (!m_dragging) return;
    m_dragging = false;
    if (m_pod && m_pod->is_alive() && m_has_press_pos) {
        // Synthesize the finger-up at the last known press position so the
        // engine never sees a stuck touch-down.
        m_pod->send_touch(2, 1, m_last_nx, m_last_ny, 0.0f, 0.0f);
    }
    m_has_press_pos = false;
}

// Map a widget point to normalized engine coords (0..1, y down).
bool PreviewView::map_to_engine(const QPointF& pos, float& nx, float& ny) const {
    if (m_frame.isNull()) return false;
    float fit = fit_scale(size(), m_frame.size());
    float scale = fit * m_zoom_pct / 100.0f;
    QSizeF disp((float)m_frame.width() * scale, (float)m_frame.height() * scale);
    QRectF target((width() - disp.width()) / 2.0 + m_pan.x(),
                  (height() - disp.height()) / 2.0 + m_pan.y(),
                  disp.width(), disp.height());
    if (!target.contains(pos)) return false;
    nx = (float)((pos.x() - target.x()) / target.width());
    ny = (float)((pos.y() - target.y()) / target.height());
    nx = qBound(0.0f, nx, 1.0f);
    ny = qBound(0.0f, ny, 1.0f);
    return true;
}

// ---------------------------------------------------------------------------
// Input forwarding (Qt → SDL keycodes)
// ---------------------------------------------------------------------------
static unsigned qt_to_sdl_key(int qt_key) {
    // ASCII region is identical between Qt key codes and SDL3 key codes
    // (letters stored lowercase by SDL, as SDL does natively).
    if (qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z) return qt_key - Qt::Key_A + 'a';
    if (qt_key >= 0x20 && qt_key <= 0x7e) return (unsigned)qt_key;

    switch (qt_key) {
        case Qt::Key_Escape:   return SDLK_ESCAPE;
        case Qt::Key_Tab:      return SDLK_TAB;
        case Qt::Key_Return:   return SDLK_RETURN;
        case Qt::Key_Enter:    return SDLK_RETURN;
        case Qt::Key_Backspace:return SDLK_BACKSPACE;
        case Qt::Key_Delete:   return SDLK_DELETE;
        case Qt::Key_Insert:   return SDLK_INSERT;
        case Qt::Key_Home:     return SDLK_HOME;
        case Qt::Key_End:      return SDLK_END;
        case Qt::Key_PageUp:   return SDLK_PAGEUP;
        case Qt::Key_PageDown: return SDLK_PAGEDOWN;
        case Qt::Key_Up:       return SDLK_UP;
        case Qt::Key_Down:     return SDLK_DOWN;
        case Qt::Key_Left:     return SDLK_LEFT;
        case Qt::Key_Right:    return SDLK_RIGHT;
        case Qt::Key_F1:  return SDLK_F1;  case Qt::Key_F2:  return SDLK_F2;
        case Qt::Key_F3:  return SDLK_F3;  case Qt::Key_F4:  return SDLK_F4;
        case Qt::Key_F5:  return SDLK_F5;  case Qt::Key_F6:  return SDLK_F6;
        case Qt::Key_F7:  return SDLK_F7;  case Qt::Key_F8:  return SDLK_F8;
        case Qt::Key_F9:  return SDLK_F9;  case Qt::Key_F10: return SDLK_F10;
        case Qt::Key_F11: return SDLK_F11; case Qt::Key_F12: return SDLK_F12;
        default: return 0;
    }
}

static unsigned qt_to_sdl_mods(Qt::KeyboardModifiers mods) {
    unsigned out = 0;
    if (mods & Qt::ShiftModifier)   out |= SDL_KMOD_SHIFT;
    if (mods & Qt::ControlModifier) out |= SDL_KMOD_CTRL;
    if (mods & Qt::AltModifier)     out |= SDL_KMOD_ALT;
    if (mods & Qt::MetaModifier)    out |= SDL_KMOD_GUI;
    return out;
}

void PreviewView::keyPressEvent(QKeyEvent* e) {
    handle_key(e, true);
    e->accept();
    QWidget::keyPressEvent(e);
}

void PreviewView::keyReleaseEvent(QKeyEvent* e) {
    handle_key(e, false);
    e->accept();
    QWidget::keyReleaseEvent(e);
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------
void PreviewView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_last_pos = e->position();
        return;
    }
    float nx, ny;
    if (m_pod && m_pod->is_alive() && map_to_engine(e->position(), nx, ny)) {
        m_dragging = true;
        m_has_press_pos = true;
        m_last_nx = nx;
        m_last_ny = ny;
        m_prev_tx = nx;
        m_prev_ty = ny;
        m_last_pos = e->position();
        // Game-native touch: SDL finger down → host maps to handleTouchEvent
        // directly (bypasses the mouse/GUI-swallow path entirely).
        m_pod->send_touch(1, 1, nx, ny, 0.0f, 0.0f);
    }
    e->accept();
}

void PreviewView::mouseMoveEvent(QMouseEvent* e) {
    if (m_panning) {
        m_pan += e->position() - m_last_pos;
        m_last_pos = e->position();
        update();
        return;
    }
    float nx, ny;
    if (m_dragging && m_pod && m_pod->is_alive() &&
        map_to_engine(e->position(), nx, ny)) {
        m_pod->send_touch(4, 1, nx, ny,
                          nx - m_prev_tx, ny - m_prev_ty);
        m_prev_tx = nx;
        m_prev_ty = ny;
    }
}

void PreviewView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        m_panning = false;
        return;
    }
    if (e->button() == Qt::LeftButton && m_dragging) {
        // Always send the finger-up, even if the pointer left the frame before
        // release (otherwise the engine would see a stuck touch-down).
        float nx, ny;
        if (!map_to_engine(e->position(), nx, ny)) {
            nx = m_last_nx;
            ny = m_last_ny;
        }
        m_dragging = false;
        m_has_press_pos = false;
        if (m_pod && m_pod->is_alive()) {
            m_pod->send_touch(2, 1, nx, ny, 0.0f, 0.0f);
        }
    }
}

void PreviewView::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        int steps = e->angleDelta().y() > 0 ? 1 : -1;
        set_zoom_percent(m_zoom_pct + steps * 25);
        e->accept();
        return;
    }
    if (m_pod && m_pod->is_alive()) {
        m_pod->send_wheel(e->angleDelta().y() > 0 ? 1 : -1);
        e->accept();
        return;
    }
    e->ignore();
}

void PreviewView::focusInEvent(QFocusEvent* e) {
    emit inputFocusChanged(true);
    QWidget::focusInEvent(e);
}
void PreviewView::focusOutEvent(QFocusEvent* e) {
    // Qt never delivers the final KeyRelease when focus leaves mid-hold —
    // synthesize the ups so the game doesn't keep walking.
    release_all_keys();
    release_mouse();
    emit inputFocusChanged(false);
    QWidget::focusOutEvent(e);
}
void PreviewView::leaveEvent(QEvent* e) {
    m_hover_active = false;
    release_all_keys();
    release_mouse();
    QWidget::leaveEvent(e);
}
void PreviewView::enterEvent(QEnterEvent* e) {
    m_hover_active = true;
    if (m_pod && m_pod->is_alive()) setFocus(Qt::MouseFocusReason);
    QWidget::enterEvent(e);
}

// App-level key redirection. Rationale: Qt only delivers key events to the
// focused widget, which is annoying for a game view — the user expects WASD to
// work the moment the cursor is over the emulator, not only after clicking it.
// The old behaviour required an explicit click (“focus lock”) to capture keys.
bool PreviewView::eventFilter(QObject* watched, QEvent* event) {
    // Only engage while the pointer hovers this viewport and the pod is up.
    const bool capturing = m_pod && m_pod->is_alive() && m_hover_active && isVisible();
    if (!capturing) return QWidget::eventFilter(watched, event);
    QPoint gp = QCursor::pos();
    if (!rect().contains(mapFromGlobal(gp))) {
        if (!m_hover_active) release_all_keys();
        return QWidget::eventFilter(watched, event);
    }

    // Block Qt shortcut/activation handling so e.g. Ctrl+S, Space or Tab reach
    // the game instead of firing menu actions while the emulator is hovered.
    if (event->type() == QEvent::ShortcutOverride) return true;

    auto* w = qobject_cast<QWidget*>(watched);
    if (!w || w == this) return false;   // direct-to-view keys take the normal path

    const bool is_key =
        event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease;
    if (!is_key) return QWidget::eventFilter(watched, event);
    // Leave shortcuts/menus/dialogs alone.
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
        return false;

    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == 0 || ke->key() == Qt::Key_unknown) return false;
    handle_key(ke, event->type() == QEvent::KeyPress);
    return true;   // consumed → the engine got it, nobody else
}

} // namespace ruby::emulator
