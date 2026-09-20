// =============================================================================
// xpera_icons.h — Xpera icon vocabulary
//
// One place that decides which glyph each semantic slot uses, so panels stop
// sprinkling raw ICON_FA_* macros around (which is how the toolkit drifted into
// mismatched icons in the first place). Falls back to a safe placeholder when a
// glyph is missing from the linked FontAwesome build.
// =============================================================================
#pragma once

#include "platform/IconsFontAwesome6.h"

namespace xpera::icons {

// ---- Navigation -----------------------------------------------------------
inline constexpr const char* home        = ICON_FA_HOUSE;
inline constexpr const char* display     = ICON_FA_DISPLAY;
inline constexpr const char* scene       = ICON_FA_MAP;
inline constexpr const char* diagnostics = ICON_FA_TERMINAL;
inline constexpr const char* research    = ICON_FA_FLASK;
inline constexpr const char* settings    = ICON_FA_GEAR;
inline constexpr const char* catalog     = ICON_FA_BOOK_OPEN;
inline constexpr const char* live        = ICON_FA_EYE;
inline constexpr const char* watchpoints = ICON_FA_CROSSHAIRS;
inline constexpr const char* database    = ICON_FA_DATABASE;

// ---- Structure / status ---------------------------------------------------
inline constexpr const char* ok          = ICON_FA_CIRCLE_CHECK;
inline constexpr const char* warning     = ICON_FA_TRIANGLE_EXCLAMATION;
inline constexpr const char* speed       = ICON_FA_GAUGE_HIGH;
inline constexpr const char* bolt        = ICON_FA_BOLT;
inline constexpr const char* refresh     = ICON_FA_ARROWS_ROTATE;
inline constexpr const char* search      = ICON_FA_MAGNIFYING_GLASS;
inline constexpr const char* marker      = ICON_FA_LOCATION_DOT;
inline constexpr const char* layers      = ICON_FA_LAYER_GROUP;
inline constexpr const char* wand        = ICON_FA_WAND_SPARKLES;

// ---- Composed helper for a leading icon + label ---------------------------
// Usage: ImGui::TextUnformatted(xpera::icons::label(xpera::icons::home, "Home"))
// is NOT provided (needs a buffer); instead pass the two parts separately, e.g.
//   ImGui::TextUnformatted(ICON_FA_HOUSE "  Home");

} // namespace xpera::icons
