#pragma once
// ============================================================================
// ruby_theme.h — Dark Studio Theme for Ruby GG (Qt6 Edition)
//   Inspired by Blender 4, Unreal Engine 5, and VS Code.
// ============================================================================

#include <QString>
#include <QColor>

namespace ruby::theme {

// Studio Color Palette (Hex)
inline constexpr const char* kBgDarkest     = "#121316"; // Viewport backdrop, outer frame
inline constexpr const char* kBgDark        = "#181a1f"; // Panel backgrounds
inline constexpr const char* kBgMedium      = "#21242b"; // Active tab, container cards
inline constexpr const char* kBgLight       = "#282c34"; // Inputs, hover states
inline constexpr const char* kBgHighlight   = "#323742"; // Selected item background

inline constexpr const char* kBorder        = "#2d313b"; // Subtle divider borders
inline constexpr const char* kBorderActive  = "#4b5263"; // Focused borders

inline constexpr const char* kTextPrimary   = "#e5e9f0"; // High-contrast headers/body
inline constexpr const char* kTextSecondary = "#9aa2b1"; // Labels, inactive tabs
inline constexpr const char* kTextDisabled  = "#5c6370"; // Disabled controls

// Brand Accent Colors
inline constexpr const char* kAccent        = "#e06c75"; // Ruby Crimson (primary brand)
inline constexpr const char* kAccentHover   = "#ef7d86";
inline constexpr const char* kAccentActive  = "#be5059";

inline constexpr const char* kSuccess       = "#98c379"; // Emerald (collision mesh, success)
inline constexpr const char* kWarning       = "#e5c07b"; // Amber (warnings)
inline constexpr const char* kError         = "#e06c75"; // Red
inline constexpr const char* kInfo          = "#61afef"; // Cyan/Blue (selection, links)

// Generates the complete modern Qt Style Sheet (QSS) for the application
inline QString get_studio_stylesheet() {
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
            background-color: #14161a;
            border-bottom: 1px solid %4;
        }
        QLabel#rubyMark { color: %5; font-size: 15px; font-weight: bold; }
        QLabel#rubyTitle { color: %2; font-size: 12px; font-weight: 700; letter-spacing: 0.5px; }
        QLabel#rubyTitleSep { color: #3b4048; font-size: 13px; margin: 0 4px; }
        QToolButton#windowControl, QToolButton#windowClose {
            border: none;
            border-radius: 0px;
            padding: 0;
            font-size: 14px;
            color: #8b949e;
            background: transparent;
        }
        QToolButton#windowControl:hover {
            background: %7;
            color: #ffffff;
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
            color: #ffffff;
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
            color: #ffffff;
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
            color: #ffffff;
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
    .arg(kBgDark)         // %1
    .arg(kTextPrimary)    // %2
    .arg(kAccent)         // %3
    .arg(kBorder)         // %4
    .arg(kAccent)         // %5
    .arg(kBgDarkest)      // %6
    .arg(kBgMedium)       // %7
    .arg(kBgLight)        // %8
    .arg(kTextSecondary)  // %9
    .arg(kBorderActive)   // %10
    .arg(kTextDisabled)   // %11
    .arg(kAccentHover)    // %12
    .arg(kAccentActive);  // %13
}

} // namespace ruby::theme
