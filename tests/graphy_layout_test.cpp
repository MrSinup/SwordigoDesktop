// ============================================================================
// graphy_layout_test.cpp — regression guard for Graphy's node geometry.
//
// The bugs this locks down (all visible in the reported scene-graph screenshot):
//   1. the italic subtitle overlapped the title / first pin row on *every* node,
//      because the subtitle was the title's band nudged down by 14px inside the
//      same 32px strip;
//   2. numeric value pills were a hard-coded 48px and clipped mid-character
//      (`5, 394.5, -2` instead of `919.5, 394.5, -29.9`, `npc_elde` for
//      `npc_elder`) because QPainter clips text to the rect it is handed;
//   3. a value already spelled out in the pin name was repeated in a second,
//      squeezed pill;
//   4. the row width came from pin count alone, so long labels ran under pills.
//
// A deterministic measurer is injected throughout, so these assertions do not
// depend on the machine's fonts or on a font database being available.
// ============================================================================

#include "ruby/graph/graphy.h"
#include "ruby/graph/graphy_layout.h"
#include "ruby/graph/graphy_canvas.h"

#include <QApplication>
#include <QFile>
#include <QImage>

#include <iostream>
#include <memory>
#include <vector>

using namespace ruby::graph;

static int g_failures = 0;
static int g_checks   = 0;

#define CHECK(cond, what)                                                        \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::cout << "  FAIL: " << (what) << "\n"                            \
                      << "        expr: " << #cond << "\n";                      \
        }                                                                        \
    } while (0)

// ── Deterministic text measurement ──────────────────────────────────────────

static TextMeasure fixed_measure() {
    return [](const QString& text, int pixel_size, bool /*bold*/, bool /*italic*/) -> float {
        return static_cast<float>(text.size()) * static_cast<float>(pixel_size) * 0.6f;
    };
}

// ── Invariants ──────────────────────────────────────────────────────────────
// Every one of these is a rule the layout engine must uphold for *any* node,
// which is why the sweep below runs them over generated shapes too.

static void check_invariants(const Node& node, const NodeGeometry& g,
                             const LayoutMetrics& m, const TextMeasure& measure,
                             const char* label) {
    const float eps = 0.01f;

    // 1. The header is two consecutive bands that never intersect.
    CHECK(!g.title_band.intersects(g.subtitle_band),
          (std::string(label) + ": title band overlaps subtitle band").c_str());
    if (!g.has_subtitle) {
        CHECK(g.subtitle_band.h == 0.0f,
              (std::string(label) + ": no subtitle but a subtitle band was reserved").c_str());
    }
    CHECK(g.header_h == m.title_h + (g.has_subtitle ? m.subtitle_h : 0.0f),
          (std::string(label) + ": header_h is not title_h (+subtitle_h)").c_str());

    // 1b. Both bands are inside the card.
    CHECK(g.title_band.y >= g.card.y - eps && g.title_band.bottom() <= g.card.bottom() + eps,
          (std::string(label) + ": title band escapes the card").c_str());
    CHECK(g.subtitle_band.bottom() <= g.card.bottom() + eps,
          (std::string(label) + ": subtitle band escapes the card").c_str());

    // 2. Every row sits strictly below the header.
    for (const auto& pg : g.inputs) {
        CHECK(pg.y - m.pin_h * 0.5f >= g.rows_top() - eps,
              (std::string(label) + ": an input row starts inside the header").c_str());
        CHECK(pg.y + m.pin_h * 0.5f <= g.card.bottom() + eps,
              (std::string(label) + ": an input row falls outside the card").c_str());
    }
    for (const auto& pg : g.outputs) {
        CHECK(pg.y - m.pin_h * 0.5f >= g.rows_top() - eps,
              (std::string(label) + ": an output row starts inside the header").c_str());
        CHECK(pg.y + m.pin_h * 0.5f <= g.card.bottom() + eps,
              (std::string(label) + ": an output row falls outside the card").c_str());
    }

    // 3 + 4. No drawn string is wider than its rect, and a label never runs
    //        under its own pill.
    for (const auto& pg : g.inputs) {
        if (!pg.label_text.isEmpty()) {
            CHECK(measure(pg.label_text, m.label_px, false, false) <= pg.label_rect.w + eps,
                  (std::string(label) + ": input label wider than its rect (would be clipped)").c_str());
        }
        CHECK(pg.label_rect.w >= -eps && pg.label_rect.h > 0.0f,
              (std::string(label) + ": degenerate input label rect").c_str());

        if (pg.has_pill) {
            CHECK(!pg.label_rect.intersects(pg.pill_rect),
                  (std::string(label) + ": input label rect intersects its pill").c_str());
            CHECK(measure(pg.value_text, m.value_px, false, false)
                      <= pg.pill_rect.w - m.pill_pad_x * 2.0f + eps,
                  (std::string(label) + ": pill value wider than the pill interior").c_str());
        }
    }
    for (const auto& pg : g.outputs) {
        if (!pg.label_text.isEmpty()) {
            CHECK(measure(pg.label_text, m.label_px, false, false) <= pg.label_rect.w + eps,
                  (std::string(label) + ": output label wider than its rect (would be clipped)").c_str());
        }
    }

    // 5. Card width respects the configured band.
    CHECK(g.card.w >= m.min_width - eps && g.card.w <= m.max_width + eps,
          (std::string(label) + ": card width outside [min_width, max_width]").c_str());

    // 6. Pin anchors follow the card, since cables and hit-testing read them.
    for (const auto& pg : g.inputs) {
        CHECK(std::abs(pg.pin_x - node.x()) < eps,
              (std::string(label) + ": input pin is not on the left border").c_str());
    }
    for (const auto& pg : g.outputs) {
        CHECK(std::abs(pg.pin_x - (node.x() + g.card.w)) < eps,
              (std::string(label) + ": output pin is not on the right border").c_str());
    }
}

// ── The node from the reported screenshot ───────────────────────────────────

static std::shared_ptr<Node> make_reported_node() {
    auto n = std::make_shared<Node>(1, QStringLiteral("elder"), QStringLiteral("NPC"));
    n->set_pos(40.0f, 60.0f);
    n->set_subtitle(QString::fromUtf8("Tpl: npc_oldman \u00b7 (Z: -29.9)"));

    n->add_input(101, QStringLiteral("Asset: npc_elder"), PinType::Object, QStringLiteral("npc_elder"));
    n->add_input(102, QStringLiteral("Position"), PinType::Vector3, QStringLiteral("919.5, 394.5, -29.9"));
    n->add_input(103, QString::fromUtf8("ID: 104 \u2192 town_part1"), PinType::Int, QStringLiteral("104"));
    n->add_input(104, QStringLiteral("Depth"), PinType::Float, QStringLiteral("-29.9"));

    n->add_output(111, QStringLiteral("Entity Ref"), PinType::Object);
    n->add_output(112, QStringLiteral("Components"), PinType::Delegate);

    return n;
}

// ── Tests ───────────────────────────────────────────────────────────────────

static void test_reported_bugs() {
    std::cout << "[graphy_layout_test] Testing the reported screenshot node...\n";

    const LayoutMetrics m;
    const TextMeasure measure = fixed_measure();
    auto n = make_reported_node();
    const NodeGeometry g = compute_node_geometry(*n, m, measure);

    check_invariants(*n, g, m, measure, "reported-node");

    // Bug 1: the header must be tall enough for both bands.
    CHECK(g.has_subtitle, "reported-node: subtitle was dropped");
    CHECK(g.header_h == m.title_h + m.subtitle_h,
          "reported-node: header does not reserve a line for the subtitle");
    CHECK(g.title_band.bottom() <= g.subtitle_band.y + 0.01f,
          "reported-node: subtitle is not below the title");

    // Bug 2: the position vector must render in full, not clipped to '5, 394.5, -2'.
    const QString vector = QStringLiteral("919.5, 394.5, -29.9");
    const PinGeometry* pos = nullptr;
    for (const auto& pg : g.inputs) {
        if (pg.label_text == QStringLiteral("Position")) pos = &pg;
    }
    CHECK(pos != nullptr, "reported-node: the Position pin vanished");
    if (pos) {
        CHECK(pos->has_pill, "reported-node: the Position pill is missing");
        CHECK(!pos->value_elided, "reported-node: the Position value had to be elided");
        CHECK(measure(vector, m.value_px, false, false)
                  <= pos->pill_rect.w - m.pill_pad_x * 2.0f + 0.01f,
              "reported-node: the Position pill is too narrow for its own value");
        CHECK(pos->pill_rect.w > 48.0f,
              "reported-node: the pill is still the old fixed 48px box");
    }

    // Bug 3: 'Asset: npc_elder' must not be printed twice.
    const PinGeometry* asset = nullptr;
    for (const auto& pg : g.inputs) {
        if (pg.label_text.startsWith(QStringLiteral("Asset"))) asset = &pg;
    }
    CHECK(asset != nullptr, "reported-node: the Asset pin vanished");
    if (asset) {
        CHECK(!asset->has_pill,
              "reported-node: the Asset value is duplicated in a redundant pill");
        CHECK(!asset->label_elided,
              "reported-node: 'Asset: npc_elder' is still being truncated");
    }

    // The same rule must not suppress a genuine, non-repeating value.
    const PinGeometry* depth = nullptr;
    for (const auto& pg : g.inputs) {
        if (pg.label_text == QStringLiteral("Depth")) depth = &pg;
    }
    CHECK(depth && depth->has_pill, "reported-node: the Depth pill was wrongly suppressed");
}

static void test_token_boundaries() {
    std::cout << "[graphy_layout_test] Testing redundant-pill token matching...\n";

    CHECK(text_contains_token(QStringLiteral("Asset: npc_elder"), QStringLiteral("npc_elder")),
          "exact token should match");
    CHECK(text_contains_token(QString::fromUtf8("ID: 104 \u2192 town_part1"), QStringLiteral("104")),
          "numeric token between spaces should match");
    CHECK(text_contains_token(QStringLiteral("Model 12"), QStringLiteral("12")),
          "trailing numeric token should match");
    CHECK(!text_contains_token(QStringLiteral("1045"), QStringLiteral("104")),
          "substring must not match without a token boundary");
    CHECK(!text_contains_token(QStringLiteral("depth_1045"), QStringLiteral("104")),
          "substring inside an identifier must not match");
    CHECK(!text_contains_token(QStringLiteral("Position"), QStringLiteral("919.5, 394.5, -29.9")),
          "a distinct value must never be treated as a repeat");
}

static void test_elision() {
    std::cout << "[graphy_layout_test] Testing ellipsis elision...\n";

    const TextMeasure measure = fixed_measure();
    const QString long_text = QStringLiteral("a very long pin label that cannot possibly fit");

    CHECK(elide_to_width(long_text, 1000.0f, 11, false, false, measure) == long_text,
          "text that fits must be returned untouched");

    const QString elided = elide_to_width(long_text, 100.0f, 11, false, false, measure);
    CHECK(elided != long_text, "text too wide must be elided");
    CHECK(elided.endsWith(QString::fromUtf8("\u2026")), "elided text must end in an ellipsis");
    CHECK(measure(elided, 11, false, false) <= 100.0f + 0.01f,
          "elided text must fit the available width");

    CHECK(elide_to_width(long_text, 0.0f, 11, false, false, measure).isEmpty(),
          "zero width must yield nothing");
    CHECK(elide_to_width(long_text, 2.0f, 11, false, false, measure).isEmpty(),
          "less than the ellipsis width must yield nothing");
}

static void test_geometry_sweep() {
    std::cout << "[graphy_layout_test] Testing invariants across generated shapes...\n";

    const LayoutMetrics m;
    const TextMeasure measure = fixed_measure();

    for (int pins = 0; pins <= 8; ++pins) {
        for (int subtitle = 0; subtitle < 2; ++subtitle) {
            for (int name_len = 1; name_len <= 40; name_len += 7) {
                auto n = std::make_shared<Node>(pins + 1,
                                                QStringLiteral("Node").left(4),
                                                QStringLiteral("Test"));
                n->set_pos(static_cast<float>(pins) * 13.0f, static_cast<float>(name_len) * 7.0f);
                if (subtitle) {
                    n->set_subtitle(QString(name_len * 3, QLatin1Char('S')));
                }

                int pin_id = 100;
                for (int p = 0; p < pins; ++p) {
                    // Deliberately mix exec pins (no pill) with typed pins that
                    // carry either a short or an over-long value.
                    const QString name(name_len, QLatin1Char('N'));
                    if (p % 3 == 0) {
                        n->add_output(pin_id++, QString(name), PinType::Object);
                    } else if (p % 3 == 1) {
                        n->add_input(pin_id++, name, PinType::Float,
                                     QString(2 * name_len, QLatin1Char('9')));
                    } else {
                        n->add_input(pin_id++, name, PinType::Exec);
                    }
                }

                const NodeGeometry g = compute_node_geometry(*n, m, measure);
                check_invariants(*n, g, m, measure, "sweep");
            }
        }
    }
}

static void test_reroute_knot() {
    std::cout << "[graphy_layout_test] Testing the reroute knot stays minimal...\n";

    const LayoutMetrics m;
    const TextMeasure measure = fixed_measure();

    auto n = std::make_shared<Node>(7, QStringLiteral("Knot"), QStringLiteral("Flow"));
    n->set_flags(NodeFlags::Reroute);
    n->add_input(1, QStringLiteral("In"), PinType::Exec);
    n->add_output(2, QStringLiteral("Out"), PinType::Exec);
    n->set_pos(5.0f, 6.0f);

    const NodeGeometry g = compute_node_geometry(*n, m, measure);
    CHECK(g.card.w == 24.0f && g.card.h == 24.0f, "knot must stay 24x24");
    CHECK(g.header_h == 0.0f, "knot must have no header band");
    CHECK(g.inputs.size() == 1 && g.outputs.size() == 1, "knot must expose one pass-through row");
}

static void test_canvas_integration() {
    std::cout << "[graphy_layout_test] Testing the canvas caches and paints that geometry...\n";

    auto graph = std::make_shared<Graph>();
    auto node = make_reported_node();
    graph->add_node(node);

    auto comment = graph->create_comment(QStringLiteral("Elder"), 0.0f, 0.0f, 320.0f, 240.0f);
    (void)comment;

    GraphyCanvas canvas;
    canvas.resize(1000, 700);
    canvas.set_graph(graph);
    // Same measurer as the pure-geometry tests, so the assertions below are
    // checking the geometry and not this machine's font metrics.
    canvas.set_text_measure(fixed_measure());

    // Rendering runs paintEvent, which is where relayout happens.
    QImage image(canvas.size(), QImage::Format_ARGB32);
    canvas.render(&image);

    const NodeGeometry* cached = canvas.node_geometry(node->id());
    CHECK(cached != nullptr, "canvas did not cache geometry for the node");
    if (cached) {
        check_invariants(*node, *cached, canvas.metrics(), canvas.text_measure(), "canvas");
    }

    // The canvas writes the computed size back onto the node, so cables and
    // hit-testing (which read node.width()/pin.canvas_*) agree with the painter.
    CHECK(node->width() == cached->card.w, "node width was not written back by the layout pass");
    CHECK(node->height() == cached->card.h, "node height was not written back by the layout pass");
    CHECK(!node->inputs().empty()
              && std::abs(node->inputs()[0].canvas_y - cached->inputs[0].y) < 0.01f,
          "pin canvas anchors were not updated from the geometry");
    CHECK(std::abs(node->outputs()[0].canvas_x
                   - (node->x() + cached->card.w)) < 0.01f,
          "output pin anchor is not on the card's right edge");

    // Stretching the metrics must re-measure, not just repaint.
    LayoutMetrics wide = canvas.metrics();
    wide.min_width = 420.0f;
    canvas.set_metrics(wide);
    const NodeGeometry* rewidened = canvas.node_geometry(node->id());
    CHECK(rewidened && rewidened->card.w >= 420.0f - 0.01f,
          "set_metrics did not re-run the layout");

    // The minimap HUD is a toggle, and an off-screen/small widget must not
    // reserve a rect for it at all.
    CHECK(canvas.minimap_visible(), "minimap should default to visible");
    canvas.toggle_minimap();
    CHECK(!canvas.minimap_visible(), "toggle_minimap did not turn the HUD off");
    canvas.set_minimap_visible(true);
    CHECK(canvas.minimap_visible(), "set_minimap_visible(true) did not turn the HUD on");

    GraphyCanvas tiny;
    tiny.resize(120, 90);
    tiny.set_graph(graph);
    tiny.set_text_measure(fixed_measure());
    QImage tiny_image(tiny.size(), QImage::Format_ARGB32);
    tiny.render(&tiny_image);
    CHECK(true, "rendering a widget smaller than the HUD must not crash");
}

/// The real graph that was on screen in the bug report: 85 scene objects with
/// 60 identifier cross-reference wires. The offline probe that diagnosed the
/// bugs found 85/85 nodes overlapping their title band; this asserts the same
/// file against the fixed engine and reports what had to elide.
static std::shared_ptr<Graph> load_real_graph() {
    const QStringList candidates = {
        QStringLiteral("../town_elderhouse_graph.json"),
        QStringLiteral("town_elderhouse_graph.json"),
        QStringLiteral("../../town_elderhouse_graph.json"),
    };
    for (const QString& c : candidates) {
        if (!QFile::exists(c)) continue;
        QFile file(c);
        if (!file.open(QIODevice::ReadOnly)) {
            CHECK(false, "could not open the real graph");
            return nullptr;
        }
        auto graph = std::make_shared<Graph>();
        if (!graph->from_json_string(QString::fromUtf8(file.readAll()))) {
            CHECK(false, "could not parse the real graph");
            return nullptr;
        }
        std::cout << "  using " << c.toStdString() << "\n";
        return graph;
    }
    std::cout << "[graphy_layout_test] (skipped) town_elderhouse_graph.json not found\n";
    return nullptr;
}

static void test_real_graph(const char* which, const TextMeasure& measure) {
    auto graph = load_real_graph();
    if (!graph) return;

    std::cout << "[graphy_layout_test] Testing the real scene graph with " << which << "...\n";

    CHECK(graph->nodes().size() > 0, "the real graph has no nodes");
    if (graph->nodes().empty()) return;

    GraphyCanvas canvas;
    canvas.resize(1600, 1000);
    canvas.set_graph(graph);
    canvas.set_text_measure(measure);

    QImage image(canvas.size(), QImage::Format_ARGB32);
    canvas.render(&image);

    int verified = 0;
    int pills = 0;
    int labels_elided = 0;   // a truncated *name* is a UX problem
    int values_elided = 0;   // a truncated *value* is what the ellipsis is for
    QStringList elided_names;
    for (const auto& n : graph->nodes()) {
        const NodeGeometry* g = canvas.node_geometry(n->id());
        CHECK(g != nullptr, "a real graph node has no cached geometry");
        if (!g) continue;

        const std::string label = std::string("real:") + n->title().toStdString();
        check_invariants(*n, *g, canvas.metrics(), canvas.text_measure(), label.c_str());
        ++verified;

        for (const auto& pg : g->inputs) {
            if (pg.has_pill) { ++pills; if (pg.value_elided) ++values_elided; }
            if (pg.label_elided) { ++labels_elided; elided_names << pg.label_text; }
        }
        for (const auto& pg : g->outputs) {
            if (pg.label_elided) { ++labels_elided; elided_names << pg.label_text; }
        }
    }

    std::cout << "  verified " << verified << " nodes, " << pills << " inline pills, "
              << labels_elided << " elided pin names, " << values_elided << " elided values\n";
    for (const QString& name : elided_names) {
        std::cout << "    elided pin name: " << name.toStdString() << "\n";
    }
    CHECK(verified == static_cast<int>(graph->nodes().size()),
          "not every real graph node was verified");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    test_reported_bugs();
    test_token_boundaries();
    test_elision();
    test_geometry_sweep();
    test_reroute_knot();
    test_canvas_integration();

    // The real 85-node scene graph, under a deterministic measurer (stable
    // numbers across machines) and under the platform's own font metrics (what
    // the user actually sees). Neither may violate a single invariant.
    test_real_graph("a deterministic measurer", fixed_measure());
    test_real_graph("the platform's font metrics", default_text_measure());

    std::cout << "[graphy_layout_test] " << (g_checks - g_failures) << "/" << g_checks
              << " checks passed";
    if (g_failures > 0) {
        std::cout << " — " << g_failures << " FAILED\n";
        return 1;
    }
    std::cout << " — all good\n";
    return 0;
}
