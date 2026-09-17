#pragma once
// ============================================================================
// graphy_layout.h — Single source of truth for Graphy node geometry.
//
// Why this file exists
// -------------------
// Graphy used to compute a node's geometry in two places with two different
// constants: `GraphyCanvas::update_node_layout()` sized the card from pin count
// with a hard-coded 32px header, while `GraphyCanvas::draw_nodes()` re-derives
// its own rects inline (`header_rect(.., 32.0f)`, a pill of exactly 48px, a
// label rect of `width * 0.5 - 14`, and a subtitle drawn with an ad-hoc
// `adjusted(12, 14, -12, 0)` nudge *inside the title's own band*). Because
// `QPainter::drawText(QRectF, flags, text)` clips to the rect it is handed,
// every one of those guesses truncates text instead of eliding it, and the
// subtitle band overlapped the title band on every single node.
//
// The fix is structural, not per-node: geometry is computed **once**, from the
// node's own text content, and both the layout pass and the painter consume the
// same result. Nothing here is a magic number tuned for one graph — widths come
// from a `TextMeasure` callback, so the same code is correct for the 6px font of
// an embedded widget and the 12px font of the standalone viewer, and it is
// testable headlessly with a deterministic measurer (no font database needed).
//
// Invariants guaranteed by `compute_node_geometry()` (asserted in
// `tests/graphy_layout_test.cpp`):
//   1. title_band and subtitle_band never intersect — they are consecutive
//      bands, not two overlays of one band.
//   2. Every pin row sits strictly below the header (`y >= card.y + header_h`).
//   3. Every drawn string is measured to fit its own rect, so an ellipsis
//      appears instead of a clipped glyph. `PinGeometry::text_fits` records it.
//   4. label_rect and pill_rect never intersect, for every row.
//   5. No pin row is drawn below `card.y + card.h`.
//
// Deliberately Qt-Gui only (no QtWidgets) so the geometry can be unit tested.
// ============================================================================

#include "graphy.h"

#include <QString>
#include <functional>
#include <vector>

namespace ruby::graph {

// ── Axis-aligned rect in canvas space ────────────────────────────────────────

struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    float right()  const { return x + w; }
    float bottom() const { return y + h; }

    /// Proper half-open overlap test (touching edges do not count as overlap).
    bool intersects(const RectF& o) const {
        return !(o.x >= right() || o.right() <= x || o.y >= bottom() || o.bottom() <= y);
    }
};

// ── Layout metrics ───────────────────────────────────────────────────────────
//
// One struct, one place to tune. Previously these were scattered as literals
// across two functions of the canvas (32, 22, 8, 48, 10, 0.40, 14, 24, 8 ...).

struct LayoutMetrics {
    // Header
    float title_h     = 18.0f;   // band reserved for the bold title
    float subtitle_h  = 14.0f;   // band reserved for the italic subtitle
    float pad_x       = 12.0f;   // header text inset from the left/right edge

    // Rows
    float pin_h       = 22.0f;   // vertical pitch of one pin row
    float pad_bottom  = 8.0f;    // breathing room under the last row
    float mid_gap     = 24.0f;   // minimum empty space between input and output halves

    // Card
    float min_width   = 190.0f;
    float max_width   = 460.0f;  // beyond this we elide rather than stretch
    float corner      = 8.0f;

    // Pin glyph + label
    float pin_radius  = 5.0f;
    float label_pad   = 10.0f;   // border -> label text
    float label_h     = 20.0f;   // height of the label rect (text is v-centred)

    // Inline value pill (the "position vector" boxes)
    float pill_h      = 14.0f;
    float pill_min_w  = 26.0f;
    float pill_pad_x  = 6.0f;    // inside the pill, left+right
    float gap         = 6.0f;    // label -> pill

    // Font pixel sizes used for measurement
    int title_px      = 12;
    int subtitle_px   = 9;
    int label_px      = 11;
    int value_px      = 9;

    /// When a pin's `default_value` merely repeats a token already present in
    /// the pin's own name (e.g. name `Asset: npc_elder`, value `npc_elder`),
    /// drawing the pill renders the same string twice, squeezed. Suppress the
    /// pill in that case and let the label use the full width.
    bool suppress_redundant_pill = true;
};

// ── Text measurement ─────────────────────────────────────────────────────────

/// Measure `text` in canvas units. Injected so the layout is deterministic in
/// tests and font-accurate in the app.
using TextMeasure = std::function<float(const QString& text, int pixel_size, bool bold, bool italic)>;

/// Query whether a pin already has a wire. Injected because an inline value
/// pill is only drawn for an *unconnected* pin, and that changes the row width.
/// When omitted, every pill is assumed to be drawn (the worst case), which is
/// what a headless geometry test wants.
using PinConnectedFn = std::function<bool(int pin_id)>;

/// Font-metrics based measurer used by the canvas. Falls back to a width
/// heuristic if no QGuiApplication exists (e.g. a bare headless harness).
TextMeasure default_text_measure();

/// Case-insensitive, token-boundary substring test: `text_contains_token("ID: 104 -> x", "104")`
/// is true, `text_contains_token("1045", "104")` is false.
bool text_contains_token(const QString& haystack, const QString& needle);

/// Truncate `text` so that it fits `avail`, appending a single ellipsis.
/// Returns an empty string when not even the ellipsis fits.
QString elide_to_width(const QString& text, float avail, int pixel_size, bool bold, bool italic,
                       const TextMeasure& measure);

// ── Computed geometry ────────────────────────────────────────────────────────

struct PinGeometry {
    int     pin_id    = 0;
    bool    is_input  = true;
    float   y         = 0.0f;   // row centre, canvas space
    float   pin_x     = 0.0f;   // circle centre, canvas space

    RectF   label_rect;
    QString label_text;         // already elided to label_rect.w
    bool    label_elided = false;

    bool    has_pill  = false;
    RectF   pill_rect;
    QString value_text;         // already elided to the pill's inner width
    bool    value_elided = false;

    /// True when the label had to be elided because it did not fit the card's
    /// maximum width — surfaced so the studio can offer a tooltip/inspector.
    bool    truncated() const { return label_elided || value_elided; }
};

struct NodeGeometry {
    RectF card;                 // the whole node card
    float header_h      = 0.0f; // title band + subtitle band
    RectF title_band;
    RectF subtitle_band;
    bool  has_subtitle  = false;

    std::vector<PinGeometry> inputs;   // index == pin index in Node::inputs()
    std::vector<PinGeometry> outputs;  // index == pin index in Node::outputs()

    /// Top of the pin-row area (== bottom of the header). The first row is
    /// centred half a `pin_h` below this.
    float rows_top() const { return card.y + header_h; }
};

/// Compute the full geometry for `node`, positioned at `node.x()/y()`.
/// Pure function: does not mutate the node and does not touch any global state.
NodeGeometry compute_node_geometry(const Node& node,
                                   const LayoutMetrics& metrics,
                                   const TextMeasure& measure,
                                   const PinConnectedFn& is_connected = {});

} // namespace ruby::graph
