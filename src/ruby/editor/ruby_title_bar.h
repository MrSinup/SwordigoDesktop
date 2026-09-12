#pragma once
// ============================================================================
// ruby_title_bar.h — Unified Custom Studio Title Bar for Frameless Ruby GG
// ============================================================================

#include <QWidget>
#include <QPoint>

class QMenuBar;
class QLabel;
class QToolButton;
class QIcon;

namespace ruby::editor {

class RubyTitleBar final : public QWidget {
    Q_OBJECT

public:
    // Window-control glyphs are PAINTED (QPainter) rather than rendered from
    // font codepoints. Font glyphs (e.g. U+2715 "✕") fall back to thin,
    // nearly-invisible strokes on many Linux font stacks — most notably on
    // KDE Plasma where the frameless window has no WM decorations either, so
    // the Close button became effectively invisible. Painted icons render
    // identically on every platform / font / compositor.
    enum IconKind {
        IconMinimize,
        IconMaximize,
        IconRestore,
        IconClose,
    };

    explicit RubyTitleBar(QWidget* parent, QMenuBar* menu_bar = nullptr);
    ~RubyTitleBar() override = default;

    // Renders one window-control icon at 2× device-pixel ratio (crisp on
    // HiDPI / Wayland fractional scaling).
    static QIcon make_control_icon(IconKind kind);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void update_maximize_button();   // swap icon between Maximize / Restore

    QPoint m_drag_offset;
    bool m_dragging = false;
    QToolButton* m_min_btn = nullptr;
    QToolButton* m_max_btn = nullptr;
    QToolButton* m_close_btn = nullptr;
};

} // namespace ruby::editor