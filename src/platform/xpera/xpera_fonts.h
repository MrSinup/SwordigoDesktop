// =============================================================================
// xpera_fonts.h — Xpera typography spec
//
// Xpera uses ONE type scale (no per-panel font experiments). This header is the
// single reference for the sizes/roles the loader should aim for; the actual
// ImFont* handles stay owned by SwordfareGUI (m_font_main / m_font_button /
// m_font_mono) so this stays dependency-free.
// =============================================================================
#pragma once

namespace xpera::type {

// Logical pixel sizes at 100% UI scale. The loader multiplies these by the
// display scale and by StyleConfig::ui_scale.
inline constexpr float size_body    = 14.0f;  // default body / list rows
inline constexpr float size_label   = 12.0f;  // section headers, captions
inline constexpr float size_title   = 18.0f;  // window titles
inline constexpr float size_mono    = 13.0f;  // console / code
inline constexpr float size_icon    = 14.0f;  // inline FA glyphs

// Roles map to the existing SwordfareGUI font slots.
enum class Role {
    Body,    // -> m_font_main
    Title,   // -> m_font_button
    Mono,    // -> m_font_mono
};

} // namespace xpera::type
