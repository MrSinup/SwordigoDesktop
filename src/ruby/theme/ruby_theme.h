#pragma once
// ============================================================================
// ruby_theme.h — Modular Theming Engine for Ruby GG Studio (Qt6 Edition)
//   Supports Dark Studio (default) and Universal White (Light Studio)
// ============================================================================

#include <QString>
#include <QColor>
#include <QApplication>
#include <QSettings>

namespace ruby::theme {

enum class ThemeId {
    DarkStudio = 0,
    LightStudio = 1
};

struct ThemePalette {
    ThemeId id;
    const char* name;
    const char* display_name;
    const char* bg_darkest;          // Outer frame, dock headers, viewport backdrop
    const char* bg_dark;             // Main window & dock panel backgrounds
    const char* bg_medium;           // Active tab, cards, menu bar, status bar
    const char* bg_light;            // Inputs, hover states, list highlights
    const char* bg_highlight;        // Selected item background
    const char* border;              // Subtle divider borders
    const char* border_active;       // Focused borders
    const char* text_primary;        // High-contrast headers/body
    const char* text_secondary;      // Labels, inactive tabs
    const char* text_disabled;       // Disabled controls
    const char* text_selected;       // Selected text color
    const char* accent;              // Primary brand accent
    const char* accent_hover;        // Accent hover
    const char* accent_active;       // Accent pressed
    const char* success;             // Collision mesh, success states
    const char* warning;             // Warning states
    const char* error;               // Error states
    const char* info;                // Info states, selection links
    const char* title_bar_bg;        // Custom title bar background
    const char* title_bar_sep;       // Title bar separator
    const char* title_control_color; // Title bar control buttons color
};

// ── Dark Studio Palette (Inspired by Blender 4, UE5, VS Code) ───────────────
inline constexpr ThemePalette kDarkStudioPalette = {
    ThemeId::DarkStudio,
    "dark_studio",
    "Dark Studio (Default)",
    "#121316", // bg_darkest
    "#181a1f", // bg_dark
    "#21242b", // bg_medium
    "#282c34", // bg_light
    "#323742", // bg_highlight
    "#2d313b", // border
    "#4b5263", // border_active
    "#e5e9f0", // text_primary
    "#9aa2b1", // text_secondary
    "#5c6370", // text_disabled
    "#ffffff", // text_selected
    "#e06c75", // accent
    "#ef7d86", // accent_hover
    "#be5059", // accent_active
    "#98c379", // success
    "#e5c07b", // warning
    "#e06c75", // error
    "#61afef", // info
    "#14161a", // title_bar_bg
    "#3b4048", // title_bar_sep
    "#8b949e"  // title_control_color
};

// ── Universal White Studio Palette (Light Mode) ─────────────────────────────
inline constexpr ThemePalette kLightStudioPalette = {
    ThemeId::LightStudio,
    "light_studio",
    "Universal White (Light Studio)",
    "#e8ebef", // bg_darkest (neutral canvas / outer frame)
    "#ffffff", // bg_dark (clean white surfaces)
    "#f1f3f6", // bg_medium (menu bar, toolbar, status bar)
    "#e4e7ec", // bg_light (inputs, hover states)
    "#cbd5e1", // bg_highlight (selected items)
    "#d8dce2", // border (subtle borders)
    "#94a3b8", // border_active (active borders)
    "#1e293b", // text_primary (high contrast slate-900)
    "#64748b", // text_secondary (slate-500 labels)
    "#94a3b8", // text_disabled (muted slate-400)
    "#0f172a", // text_selected
    "#d32f2f", // accent (brand crimson tuned for light mode)
    "#e53935", // accent_hover
    "#b71c1c", // accent_active
    "#2e7d32", // success
    "#d97706", // warning
    "#d32f2f", // error
    "#0284c7", // info
    "#f1f3f6", // title_bar_bg
    "#cbd5e1", // title_bar_sep
    "#475569"  // title_control_color
};

// Backwards-compatible raw constants for dark theme
inline constexpr const char* kBgDarkest     = kDarkStudioPalette.bg_darkest;
inline constexpr const char* kBgDark        = kDarkStudioPalette.bg_dark;
inline constexpr const char* kBgMedium      = kDarkStudioPalette.bg_medium;
inline constexpr const char* kBgLight       = kDarkStudioPalette.bg_light;
inline constexpr const char* kBgHighlight   = kDarkStudioPalette.bg_highlight;
inline constexpr const char* kBorder        = kDarkStudioPalette.border;
inline constexpr const char* kBorderActive  = kDarkStudioPalette.border_active;
inline constexpr const char* kTextPrimary   = kDarkStudioPalette.text_primary;
inline constexpr const char* kTextSecondary = kDarkStudioPalette.text_secondary;
inline constexpr const char* kTextDisabled  = kDarkStudioPalette.text_disabled;
inline constexpr const char* kAccent        = kDarkStudioPalette.accent;
inline constexpr const char* kAccentHover   = kDarkStudioPalette.accent_hover;
inline constexpr const char* kAccentActive  = kDarkStudioPalette.accent_active;
inline constexpr const char* kSuccess       = kDarkStudioPalette.success;
inline constexpr const char* kWarning       = kDarkStudioPalette.warning;
inline constexpr const char* kError         = kDarkStudioPalette.error;
inline constexpr const char* kInfo          = kDarkStudioPalette.info;

inline const ThemePalette& get_palette(ThemeId id) {
    if (id == ThemeId::LightStudio) {
        return kLightStudioPalette;
    }
    return kDarkStudioPalette;
}

// Generates the complete modern Qt Style Sheet (QSS) for any given ThemePalette
inline QString generate_stylesheet(const ThemePalette& p) {
    return QString(R"(
        /* Global Reset & Base */
        QWidget {
            background-color: %1;
            color: %2;
            font-family: "Inter", "Segoe UI", "Cantarell", "Ubuntu", sans-serif;
            font-size: 12px;
            selection-background-color: %3;
            selection-color: #ffffff;
            outline: none;
        }

        /* Main Window & Docking */
        QMainWindow {
            background-color: %1;
        }

        QWidget#rubyTitleBar {
            background-color: %14;
            border-bottom: 1px solid %4;
        }
        QLabel#rubyMark { color: %5; font-size: 15px; font-weight: bold; }
        QLabel#rubyTitle { color: %2; font-size: 12px; font-weight: 700; letter-spacing: 0.5px; }
        QLabel#rubyTitleSep { color: %15; font-size: 13px; margin: 0 4px; }
        QToolButton#windowControl, QToolButton#windowClose {
            border: none;
            border-radius: 0px;
            padding: 0;
            font-size: 14px;
            color: %16;
            background: transparent;
        }
        QToolButton#windowControl:hover {
            background: %7;
            color: %2;
        }
        QToolButton#windowClose:hover {
            background: #e81123;
            color: #ffffff;
        }

        QMainWindow::separator {
            background: %4;
            width: 2px;
            height: 2px;
        }

        QMainWindow::separator:hover {
            background: %5;
        }

        /* Tooltips */
        QToolTip {
            background-color: %7;
            color: %2;
            border: 1px solid %10;
            border-radius: 6px;
            padding: 6px 10px;
            font-size: 11px;
        }

        /* Dock Widgets */
        QDockWidget {
            background-color: %1;
            color: %2;
            border: 1px solid %4;
        }

        QDockWidget::title {
            background-color: %6;
            color: %9;
            text-align: left;
            padding: 6px 10px;
            border-bottom: 1px solid %4;
            font-weight: 600;
            font-size: 11px;
            letter-spacing: 0.5px;
        }

        QDockWidget::close-button, QDockWidget::float-button {
            border: none;
            background: transparent;
            padding: 2px;
            border-radius: 3px;
        }

        QDockWidget::close-button:hover, QDockWidget::float-button:hover {
            background-color: %7;
        }

        /* Menu Bar */
        QMenuBar {
            background-color: %6;
            color: %2;
            border-bottom: 1px solid %4;
            padding: 2px 6px;
        }

        QMenuBar::item {
            background: transparent;
            padding: 4px 8px;
            border-radius: 4px;
        }

        QMenuBar::item:selected {
            background-color: %7;
        }

        QMenu {
            background-color: %6;
            color: %2;
            border: 1px solid %4;
            border-radius: 6px;
            padding: 4px;
        }

        QMenu::item {
            padding: 6px 24px 6px 12px;
            border-radius: 4px;
        }

        QMenu::item:selected {
            background-color: %5;
            color: #ffffff;
        }

        QMenu::separator {
            height: 1px;
            background: %4;
            margin: 4px 8px;
        }

        /* ToolBar */
        QToolBar {
            background-color: %6;
            border-bottom: 1px solid %4;
            spacing: 4px;
            padding: 4px;
        }

        QToolButton {
            background-color: transparent;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 4px 8px;
            color: %2;
        }

        QToolButton:hover {
            background-color: %7;
            border-color: %4;
        }

        QToolButton:pressed {
            background-color: %8;
        }

        QToolButton:checked {
            background-color: %8;
            border-color: %5;
            color: %2;
        }

        /* Status Bar */
        QStatusBar {
            background-color: %6;
            color: %9;
            border-top: 1px solid %4;
            font-size: 11px;
        }

        /* Push Buttons */
        QPushButton {
            background-color: %7;
            color: %2;
            border: 1px solid %4;
            border-radius: 4px;
            padding: 6px 14px;
            font-weight: 500;
        }

        QPushButton:hover {
            background-color: %8;
            border-color: %10;
        }

        QPushButton:pressed {
            background-color: %6;
        }

        QPushButton:disabled {
            background-color: %6;
            color: %11;
            border-color: %4;
        }

        /* Brand Action Button */
        QPushButton#brandButton {
            background-color: %5;
            color: #ffffff;
            border: 1px solid %5;
        }

        QPushButton#brandButton:hover {
            background-color: %12;
        }

        QPushButton#brandButton:pressed {
            background-color: %13;
        }

        /* Input Controls */
        QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox {
            background-color: %6;
            color: %2;
            border: 1px solid %4;
            border-radius: 4px;
            padding: 4px 8px;
        }

        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {
            border: 1px solid %5;
        }

        /* Tree & List Views (Scene Hierarchy, Asset Browser) */
        QTreeView, QListView, QTableView {
            background-color: %6;
            color: %2;
            border: 1px solid %4;
            border-radius: 4px;
            show-decoration-selected: 1;
        }

        QTreeView::item, QListView::item {
            padding: 4px 6px;
            border-radius: 3px;
        }

        QTreeView::item:hover, QListView::item:hover {
            background-color: %7;
        }

        QTreeView::item:selected, QListView::item:selected {
            background-color: %8;
            color: %17;
        }

        QHeaderView::section {
            background-color: %1;
            color: %9;
            padding: 4px 8px;
            border: none;
            border-right: 1px solid %4;
            border-bottom: 1px solid %4;
            font-weight: 600;
        }

        /* Tab Widget */
        QTabWidget::pane {
            border: 1px solid %4;
            background-color: %1;
            top: -1px;
        }

        QTabBar::tab {
            background-color: %6;
            color: %9;
            border: 1px solid %4;
            border-bottom: none;
            padding: 6px 14px;
            margin-right: 2px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }

        QTabBar::tab:hover {
            background-color: %7;
            color: %2;
        }

        QTabBar::tab:selected {
            background-color: %1;
            color: %2;
            border-bottom: 1px solid %1;
            font-weight: 500;
        }

        /* Scrollbars (Sleek minimalist dark) */
        QScrollBar:vertical {
            background-color: %6;
            width: 10px;
            margin: 0px;
        }

        QScrollBar::handle:vertical {
            background-color: %7;
            min-height: 20px;
            border-radius: 5px;
            margin: 2px;
        }

        QScrollBar::handle:vertical:hover {
            background-color: %10;
        }

        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }

        QScrollBar:horizontal {
            background-color: %6;
            height: 10px;
            margin: 0px;
        }

        QScrollBar::handle:horizontal {
            background-color: %7;
            min-width: 20px;
            border-radius: 5px;
            margin: 2px;
        }

        QScrollBar::handle:horizontal:hover {
            background-color: %10;
        }

        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
        }

        /* Splitters */
        QSplitter::handle {
            background-color: %4;
        }

        QSplitter::handle:horizontal {
            width: 3px;
        }

        QSplitter::handle:vertical {
            height: 3px;
        }

        QSplitter::handle:hover {
            background-color: %5;
        }
    )")
    .arg(p.bg_dark)             // %1
    .arg(p.text_primary)        // %2
    .arg(p.accent)              // %3
    .arg(p.border)              // %4
    .arg(p.accent)              // %5
    .arg(p.bg_darkest)          // %6
    .arg(p.bg_medium)           // %7
    .arg(p.bg_light)            // %8
    .arg(p.text_secondary)      // %9
    .arg(p.border_active)       // %10
    .arg(p.text_disabled)       // %11
    .arg(p.accent_hover)        // %12
    .arg(p.accent_active)       // %13
    .arg(p.title_bar_bg)        // %14
    .arg(p.title_bar_sep)       // %15
    .arg(p.title_control_color) // %16
    .arg(p.text_selected);      // %17
}

inline QString get_theme_stylesheet(ThemeId id) {
    return generate_stylesheet(get_palette(id));
}

// Backwards-compatible call for studio dark stylesheet
inline QString get_studio_stylesheet() {
    return get_theme_stylesheet(ThemeId::DarkStudio);
}

// ── Theme State & Persistence ───────────────────────────────────────────────

inline ThemeId get_current_theme() {
    QSettings settings;
    int val = settings.value("ruby_gg/theme", static_cast<int>(ThemeId::DarkStudio)).toInt();
    if (val == static_cast<int>(ThemeId::LightStudio)) {
        return ThemeId::LightStudio;
    }
    return ThemeId::DarkStudio;
}

inline void set_current_theme(ThemeId id) {
    QSettings settings;
    settings.setValue("ruby_gg/theme", static_cast<int>(id));
}

inline void apply_theme(ThemeId id) {
    set_current_theme(id);
    if (qApp) {
        qApp->setStyleSheet(get_theme_stylesheet(id));
    }
}

inline void init_theme() {
    ThemeId id = get_current_theme();
    apply_theme(id);
}

} // namespace ruby::theme
