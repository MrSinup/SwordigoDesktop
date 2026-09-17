// ============================================================================
// graphy_layout.cpp — Single-authority node geometry for Graphy.
// See graphy_layout.h for the invariants this file is required to uphold.
// ============================================================================

#include "graphy_layout.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>

#include <algorithm>
#include <cmath>

namespace ruby::graph {

namespace {

const QString kEllipsis = QString::fromUtf8("\u2026");

/// Number of pin rows in a node (inputs and outputs are drawn on shared rows).
size_t row_count(const Node& node) {
    return std::max(node.inputs().size(), node.outputs().size());
}

/// Should this pin render an inline value pill?
/// Mirrors the painter's rule (unconnected, non-exec, has a default) so the
/// measured width always matches what is actually drawn.
bool pin_has_pill(const Pin& pin, const LayoutMetrics& m, const PinConnectedFn& is_connected) {
    if (pin.type == PinType::Exec) return false;
    if (pin.dir == PinDirection::Output) return false;
    if (pin.default_value.isEmpty()) return false;
    if (is_connected && is_connected(pin.id)) return false;
    if (m.suppress_redundant_pill && text_contains_token(pin.name, pin.default_value)) return false;
    return true;
}

/// Natural (unclamped) pill width for a value string.
float natural_pill_width(const Pin& pin, const LayoutMetrics& m, const TextMeasure& measure) {
    const float text_w = measure(pin.default_value, m.value_px, false, false);
    return std::max(m.pill_min_w, text_w + m.pill_pad_x * 2.0f);
}

bool is_word_char(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

} // namespace

// ── Text helpers ─────────────────────────────────────────────────────────────

bool text_contains_token(const QString& haystack, const QString& needle) {
    if (needle.isEmpty() || haystack.isEmpty()) return false;
    if (needle.size() > haystack.size()) return false;

    const int n = haystack.size();
    const int m = needle.size();

    for (int i = 0; i + m <= n; ++i) {
        bool hit = true;
        for (int j = 0; j < m; ++j) {
            if (haystack.at(i + j).toCaseFolded() != needle.at(j).toCaseFolded()) {
                hit = false;
                break;
            }
        }
        if (!hit) continue;

        // Require token boundaries so "104" does not match inside "1045".
        const bool left_ok  = (i == 0)     || !is_word_char(haystack.at(i - 1));
        const bool right_ok = (i + m == n) || !is_word_char(haystack.at(i + m));
        if (left_ok && right_ok) return true;
    }
    return false;
}

QString elide_to_width(const QString& text, float avail, int pixel_size, bool bold, bool italic,
                       const TextMeasure& measure) {
    if (text.isEmpty()) return QString();
    if (avail <= 0.0f) return QString();
    if (measure(text, pixel_size, bold, italic) <= avail) return text;

    const float ell_w = measure(kEllipsis, pixel_size, bold, italic);
    if (ell_w >= avail) return QString();

    // Largest prefix whose width plus the ellipsis still fits.
    int lo = 0;
    int hi = text.size();
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        const float w = measure(text.left(mid), pixel_size, bold, italic) + ell_w;
        if (w <= avail) lo = mid;
        else            hi = mid - 1;
    }
    return text.left(lo) + kEllipsis;
}

TextMeasure default_text_measure() {
    return [](const QString& text, int pixel_size, bool bold, bool italic) -> float {
        if (text.isEmpty()) return 0.0f;

        // No QGuiApplication (bare headless harness): cheap proportional estimate
        // so layout still produces sane, non-overlapping geometry.
        if (!qGuiApp) {
            return static_cast<float>(text.size()) * static_cast<float>(pixel_size) * 0.62f;
        }

        static QHash<quint32, QFontMetricsF> s_font_metrics;
        const quint32 key = (static_cast<quint32>(pixel_size) << 2)
                          | (bold ? 0x2u : 0x0u)
                          | (italic ? 0x1u : 0x0u);

        auto it = s_font_metrics.find(key);
        if (it == s_font_metrics.end()) {
            QFont f = QGuiApplication::font();
            f.setPixelSize(pixel_size);
            f.setBold(bold);
            f.setItalic(italic);
            it = s_font_metrics.insert(key, QFontMetricsF(f));
        }
        return static_cast<float>(it.value().horizontalAdvance(text));
    };
}

// ── Geometry ─────────────────────────────────────────────────────────────────

NodeGeometry compute_node_geometry(const Node& node,
                                   const LayoutMetrics& m,
                                   const TextMeasure& measure,
                                   const PinConnectedFn& is_connected) {
    NodeGeometry g;

    // ── Reroute knot (UK2Node_Knot): a fixed 24x24 dot with one pass-through row
    if (node.flags() & NodeFlags::Reroute) {
        g.card = RectF{node.x(), node.y(), 24.0f, 24.0f};
        g.header_h = 0.0f;
        g.has_subtitle = false;

        PinGeometry in;
        in.is_input = true;
        in.y = node.y() + 12.0f;
        in.pin_x = node.x() + 12.0f;
        if (!node.inputs().empty()) in.pin_id = node.inputs()[0].id;
        g.inputs.push_back(in);

        PinGeometry out;
        out.is_input = false;
        out.y = node.y() + 12.0f;
        out.pin_x = node.x() + 12.0f;
        if (!node.outputs().empty()) out.pin_id = node.outputs()[0].id;
        g.outputs.push_back(out);

        return g;
    }

    // ── Header: two consecutive bands, never two overlays of one band ────────
    g.has_subtitle = !node.subtitle().isEmpty();
    g.header_h = m.title_h + (g.has_subtitle ? m.subtitle_h : 0.0f);

    const size_t rows = row_count(node);

    // ── Pass 1: natural width, driven by measured text ──────────────────────
    float need_w = m.min_width;

    // Header text can never be cut off by the card edge.
    need_w = std::max(need_w, m.pad_x + measure(node.title(), m.title_px, true, false) + m.pad_x);
    if (g.has_subtitle) {
        need_w = std::max(need_w,
                          m.pad_x + measure(node.subtitle(), m.subtitle_px, false, true) + m.pad_x);
    }

    for (size_t i = 0; i < rows; ++i) {
        const Pin* in  = (i < node.inputs().size())  ? &node.inputs()[i]  : nullptr;
        const Pin* out = (i < node.outputs().size()) ? &node.outputs()[i] : nullptr;

        float left  = 0.0f;
        float right = 0.0f;

        if (in) {
            left = m.label_pad + measure(in->name, m.label_px, false, false);
            if (pin_has_pill(*in, m, is_connected)) {
                left += m.gap + natural_pill_width(*in, m, measure);
            }
        }
        if (out) {
            right = m.label_pad + measure(out->name, m.label_px, false, false);
        }

        const float row_need = left + ((left > 0.0f && right > 0.0f) ? m.mid_gap : 0.0f) + right;
        need_w = std::max(need_w, row_need);
    }

    const float node_w = std::min(std::max(need_w, m.min_width), m.max_width);
    const float node_h = g.header_h + static_cast<float>(rows) * m.pin_h + m.pad_bottom;

    g.card = RectF{node.x(), node.y(), node_w, node_h};
    g.title_band = RectF{node.x() + m.pad_x, node.y(),
                         std::max(0.0f, node_w - m.pad_x * 2.0f), m.title_h};
    g.subtitle_band = RectF{node.x() + m.pad_x, node.y() + m.title_h,
                            std::max(0.0f, node_w - m.pad_x * 2.0f),
                            g.has_subtitle ? m.subtitle_h : 0.0f};

    // Rows are bounded by *that row's* other half, not by the card's midpoint.
    // Splitting at the midpoint starved long input labels on rows whose output
    // label was short — which is exactly how `Asset: npc_elde` and
    // `5, 394.5, -2` came about. Because `node_w` was sized from the widest row
    // and (when it was not capped) is at least
    // `left_extent + mid_gap + right_extent`, this bound lets both halves take
    // their natural width while still guaranteeing `mid_gap` of separation;
    // when the width *was* capped it degrades into honest elision.
    const float right_edge = node.x() + node_w;

    // ── Pass 2: per-row rects, every string elided to its own rect ──────────
    g.inputs.reserve(node.inputs().size());
    g.outputs.reserve(node.outputs().size());

    for (size_t i = 0; i < rows; ++i) {
        const float row_y = node.y() + g.header_h + static_cast<float>(i) * m.pin_h + m.pin_h * 0.5f;

        const bool has_in  = (i < node.inputs().size());
        const bool has_out = (i < node.outputs().size());

        // Right edge of this row's input block, needed by the output half.
        float in_block_end = node.x() + m.label_pad;

        if (has_in) {
            const Pin& pin = node.inputs()[i];

            const float out_extent = has_out
                ? m.label_pad + measure(node.outputs()[i].name, m.label_px, false, false)
                : 0.0f;
            const float in_limit = right_edge - out_extent - ((has_in && has_out) ? m.mid_gap : 0.0f);

            const float label_x       = node.x() + m.label_pad;
            const float avail         = std::max(0.0f, in_limit - label_x);
            const float label_natural = measure(pin.name, m.label_px, false, false);

            // A pin's own name outranks its value: a wide vector may shrink the
            // pill, but it may never squeeze the label that names the pin.
            float pill_w = 0.0f;
            if (pin_has_pill(pin, m, is_connected)) {
                const float pill_natural = natural_pill_width(pin, m, measure);
                const float room = std::max(0.0f, avail - label_natural - m.gap);
                pill_w = std::min(pill_natural, room);
                if (pill_w < m.pill_min_w) pill_w = 0.0f;   // not worth a squashed box
            }

            PinGeometry pg;
            pg.pin_id   = pin.id;
            pg.is_input = true;
            pg.y        = row_y;
            pg.pin_x    = node.x();   // circle centred on the border, as before

            const float label_avail = std::max(0.0f, avail - ((pill_w > 0.0f) ? (pill_w + m.gap) : 0.0f));

            pg.label_text = elide_to_width(pin.name, label_avail, m.label_px, false, false, measure);
            pg.label_elided = (pg.label_text != pin.name);

            const float label_w = pg.label_text.isEmpty()
                                    ? 0.0f
                                    : measure(pg.label_text, m.label_px, false, false);
            pg.label_rect = RectF{label_x, row_y - m.label_h * 0.5f, label_w, m.label_h};
            in_block_end  = label_x + label_w;

            if (pill_w > 0.0f) {
                const float pill_x = label_x + label_w + m.gap;
                pg.has_pill  = true;
                pg.pill_rect = RectF{pill_x, row_y - m.pill_h * 0.5f, pill_w, m.pill_h};

                const float inner = std::max(0.0f, pill_w - m.pill_pad_x * 2.0f);
                pg.value_text = elide_to_width(pin.default_value, inner, m.value_px, false, false, measure);
                pg.value_elided = (pg.value_text != pin.default_value);

                in_block_end = pill_x + pill_w;
            }

            g.inputs.push_back(pg);
        }

        if (has_out) {
            const Pin& pin = node.outputs()[i];

            const float label_end   = right_edge - m.label_pad;
            const float label_avail = std::max(0.0f, label_end - in_block_end - (has_in ? m.mid_gap : 0.0f));

            PinGeometry pg;
            pg.pin_id   = pin.id;
            pg.is_input = false;
            pg.y        = row_y;
            pg.pin_x    = right_edge;   // circle centred on the right border

            pg.label_text = elide_to_width(pin.name, label_avail, m.label_px, false, false, measure);
            pg.label_elided = (pg.label_text != pin.name);

            const float label_w = pg.label_text.isEmpty()
                                    ? 0.0f
                                    : measure(pg.label_text, m.label_px, false, false);

            // Right-aligned: the rect is exact, so nothing can be clipped.
            pg.label_rect = RectF{label_end - label_w, row_y - m.label_h * 0.5f, label_w, m.label_h};

            g.outputs.push_back(pg);
        }
    }

    return g;
}

} // namespace ruby::graph
