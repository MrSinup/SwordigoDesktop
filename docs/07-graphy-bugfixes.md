# 07 — Graphy Bug Fixes: root causes as layout-engine rules

Every fix below is a **rule in the layout engine**, not a per-node patch. That distinction is
the whole point: the reported bugs are consistent across every node because they come from
four unconditional constants, not from bad data.

---

## 0. Method, and the exact evidence used

**The screenshot's source graph is identified.** The reported node subtitles
(`Tpl: npc_oldman · (Z: -29.9)`, `ID: 101`, `ID: 104 → town_part1`) and values
(`Asset: npc_elder` / `npc_elder`, `…, 394.5, -2`) all appear verbatim in
`town_elderhouse_graph.json` (85 nodes, 60 connections, 36 comment frames) — that file is in
the repo root and is the output of `tools/scene_to_graph.py` on the elder-house scene. No
guessing about which graph we are fixing.

**The layout arithmetic was reproduced offline.**
`.scratch/graphy_recon/graphy_layout_probe.py` mirrors `GraphyCanvas::update_node_layout()`
and `GraphyCanvas::draw_nodes()` exactly and reports, against the real graph:

```
graph: town_elderhouse_graph.json
nodes=85 connections=60 comments=36

[BUG 1] nodes whose title band and subtitle band share pixels: 85/85
        reserved height for title+subtitle: 32px total
        (title occupies all 32, subtitle starts at +14) -> 18px of guaranteed overlap

[BUG 2/3] pills whose text is wider than the 48px box: 88
        'Entity: elder'      'Pos (919, 394)'      '919.5, 394.5, -29.9'  needs  93.1px -> shows ~', 394.5,'
        'Model'              'Asset: npc_elder'    'npc_elder'            needs  44.1px -> shows ~'npc_elde'
        'Light'              'Intensity: 0.30'     '0.300000012'          needs  53.9px -> shows ~'.3000000'
        ... and 76 more

[BUG 2] pin-name label rect overlapping the value pill: 117
[INFO]   nodes with JSON height leaving >8px dead band below Graphy's own calc: 13
```

`'Asset: npc_elder'` rendering its value as `npc_elde` is **exactly** the reported symptom,
which validates the model of the bug.

**Static confirmation of the mechanism**, from `src/ruby/graph/graphy_canvas.cpp`:

```
$ grep -n "setClipRect\|QFontMetrics\|elidedText\|setElideMode" src/ruby/graph/*.cpp
(no matches)
```

There is **no font measurement and no clipping anywhere in Graphy**. Every rect is a
hard-coded constant. That single fact explains all three text bugs at once, and means they
are worse on HiDPI displays (fonts scale with DPI, the constants do not).

---

## 1. Bug 1 — header/subtitle collision

### Root cause

`update_node_layout()` (line 112) hard-codes the header:

```cpp
const float header_h = 32.0f;
const float pin_h    = 22.0f;
const float pad_bot  = 8.0f;
float calc_h = header_h + (float)rows * pin_h + pad_bot;
node.set_size(std::max(node.width(), 190.0f), std::max(node.height(), calc_h));
float cur_y = node.y() + header_h + pin_h * 0.5f;      // first pin row
```

and `draw_nodes()` (lines 530-547) then draws **two** text baselines into that one band:

```cpp
QRectF header_rect(n->x(), n->y(), n->width(), 32.0f);
… title …
p.drawText(header_rect.adjusted(12, 0, -12, 0),  Qt::AlignVCenter | Qt::AlignLeft, n->title());
… subtitle …
p.drawText(header_rect.adjusted(12, 14, -12, 0), Qt::AlignVCenter | Qt::AlignLeft, n->subtitle());
```

The `+14` is an ad-hoc nudge that moves the subtitle's *vertically-centred* band down by
14px **inside a rect that is still 18px tall** (32 − 14). So:

- title band = `[y, y+32)`, vertically centred → glyph box ≈ `[y+8.5, y+23.5)` for a 12px font
- subtitle band = `[y+14, y+32)`, centred → glyph box ≈ `[y+17.5, y+28.5)` for a 9px font
- **glyph overlap ≈ 6px**, rect overlap = 18px, on **85/85 nodes**.

There is a second, structural defect: **the header height does not depend on whether a
subtitle exists.** A node without a subtitle wastes the same 32px, and a node with a long
subtitle has no more room than one with `ID: 101`. The layout is not a function of content.

### Fix rule (layout engine)

> **Rule L1 — the header's height is the sum of the heights of the lines it contains.**

```cpp
struct HeaderMetrics {
    int title_h    = fm_title.height();                       // ascent + descent
    int subtitle_h = subtitle.isEmpty() ? 0 : fm_subtitle.height();
    int pad_y      = 6;
    int gap        = subtitle.isEmpty() ? 0 : 2;
    int height() const { return pad_y * 2 + title_h + gap + subtitle_h; }
};
```

For DejaVu Sans at 12px/9px this gives `6+15+2+12+6 = 41px` with a subtitle and `27px`
without — versus the current unconditional 32. The shortfall on subtitle-bearing nodes is
**9px**, which is the reported collision.

Consequences that must follow:

- `pin_y_0 = node.y() + header_height() + pin_h * 0.5f`, using the measured header.
- The node's minimum height becomes `header_height() + rows * row_height() + pad_bot`,
  where `row_height()` is measured (§2), not the constant 22.
- Title and subtitle are drawn in **disjoint** rects: title in
  `[y+pad_y, y+pad_y+title_h)`, subtitle in `[y+pad_y+title_h+gap, … +subtitle_h)`.
  No `Qt::AlignVCenter` into a shared band, ever.
- The subtitle is elided with `Qt::ElideRight` to the available width (see §2 rule L3),
  because a long `Tpl: … · (Z: …) · …` must not push the header taller than one line.

### Why this is not a per-node patch

The overlap is 85/85. A per-node fix would require 85 changes and would break on the next
data change. Rule L1 is content-driven, so it holds for any graph.

---

## 2. Bug 2 — truncated numeric pills, no ellipsis, and label/pill collision

### Root cause (two independent defects)

**(a) The pill has a hard-coded width and clips instead of eliding.**

```cpp
float pill_w = 48.0f;
QRectF pill_r(pin.canvas_x + n->width() * 0.40f, pin.canvas_y - 7.0f, pill_w, 14.0f);
…
QFont f_val = f_pin; f_val.setPixelSize(9);
p.drawText(pill_r, Qt::AlignCenter, pin.default_value);
```

`QPainter::drawText(rect, flags, text)` **clips**; it never elides. `Qt::AlignCenter` centres
the *full* string and then clips both ends, so a half-visible number appears from the middle
outwards — which is precisely the reported `5, 394.5, -2` from `919.5, 394.5, -29.9`, and
`7.5, 116.4, -` from `-257.5, 116.4, -65.9`. 88 pills exceed 48px; the worst needs 127px.

**(b) The pill is positioned as a fraction of node width, so it lands under the label.**

```cpp
QRectF txt_r(pin.canvas_x + 10.0f, pin.canvas_y - 10.0f, n->width() * 0.5f - 14.0f, 20.0f);
```

For a 240px-wide node the label region is `[x+10, x+116)` and the pill is
`[x+96, x+144)` — an **overlap of 20px**, on 117 rows. So even short values get drawn on top
of their own labels. Note also that the pill starts at 40% of the width, leaving
`240 − 144 = 96px` of the card completely unused: the layout is simultaneously too cramped
and too wide.

### Fix rules

> **Rule L2 — a pill's width is `min(max_w, text_advance + 2*pad)`; if the text exceeds
> `max_w`, it is elided with `Qt::ElideMiddle`, never clipped.**

```cpp
const int adv = fm_pill.horizontalAdvance(value);
const int w   = std::min(kMaxPillW, adv + 2 * kPillPadX);
QString shown = (adv + 2 * kPillPadX <= kMaxPillW)
                  ? value
                  : fm_pill.elidedText(value, Qt::ElideMiddle, kMaxPillW - 2 * kPillPadX);
```

`Qt::ElideMiddle` is chosen over `ElideRight` deliberately: for `919.5, 394.5, -29.9` the
informative parts are the magnitude and the depth, so `919.5…-29.9` is a better preview than
`919.5, 394.5, …`. With L2, capacity is ~9-10 characters at 9px, so `npc_elder` (44px,
capacity 10 chars ⇒ fits) renders whole, and the three-component position vectors elide
legibly.

> **Rule L3 — a node's row is laid out left-to-right/right-to-left, not at fractional offsets.**

Row geometry, per pin row `r`:

```
[label_r ][gutter][ pill_r ]                    <- input row   (label left-aligned, pill right-aligned)
[ pill_out ][gutter][ label_right ]             <- output row  (pill left-aligned, label right-aligned)
```

- `label_r` starts at `x + kPadX`. Its available width is
  `node_width - 2*kPadX - kGutter - pill_width - kPortColumn`.
- `text_r` is elided to its available width with `Qt::ElideRight`.
- The pill's right edge is `x + node_width - kPadX - kPortColumn`, **right-aligned**, so the
  unused 96px disappears and the label/pill overlap becomes structurally impossible.

> **Rule L4 — `node_width = max(kMinW, kPadX*2 + maxInputRowWidth + kGutter + maxOutputRowWidth)`.**
> Width becomes a function of content, exactly like height in Rule L1.

> **Rule L5 — all text is measured once per (font, string) and cached.**
> `QFontMetrics` construction is not free; a `struct TextCache { QHash<QString,int> advance; }`
> per `(font, dpr)` keeps a 85-node repaint cheap.

> **Rule L6 — metrics are recomputed when `devicePixelRatio` or the font changes.**
> Connect to `QWidget::screenChanged` / `QEvent::FontChange` / `QEvent::DevicePixelRatioChange`
> and invalidate the layout. This is what stops the bug from reappearing on HiDPI.

Rule L4 has an important consequence worth stating so the fix is not over-engineered:
**after Rules L2/L3, almost nothing needs to be wider.** With the redundant labels removed
(§3), the widest row in `town_elderhouse_graph.json` is the entity position row:
`Pos` (≈25px) + gutter (8) + pill for `919.5, 394.5, -29.9` (93px) + padding ≈ 132px, which
fits inside the current 240px cards. So the fix is *not* "make nodes enormous" — it is "use
the width the node already has".

---

## 3. Bug 3 — duplicated value tags

### Root cause

Strictly a data-shape bug in `tools/scene_to_graph.py`, exposed by `draw_nodes()`:

```python
c_inputs.append({
    "id": pid,
    "node_id": cnode_id,
    "name": f"Asset: {name_val}",        # <-- value embedded in the LABEL
    "tooltip": f"Asset Name: {name_val}",
    "type": PIN_STRING,
    "dir": PIN_DIR_INPUT,
    "default_value": name_val            # <-- and again as the pill VALUE
})
```

The renderer then draws `pin.name` (`Asset: npc_elder`) and, because
`!conn && !pin.default_value.isEmpty()`, also draws the pill (`npc_elder`, clipped to
`npc_elde`). Same node, same value, twice, both squeezed. Identical construction for
`Tex: grasslandsbackground_day`, `Intensity: 0.30`, `Target: town_part1`.

### Fix rule

> **Rule L7 — a pin's label and its value are separate fields; a label never contains the
> value.**

The `Pin` struct (`graphy.h`) already has both `name` and `default_value`; the fix is to stop
duplicating the value into `name` **and** to give the renderer an explicit contract:

```cpp
struct Pin {
    …
    QString name;                    // "Asset"        (short, static)
    QString default_value;           // "npc_elder"    (dynamic)
    bool    show_label = true;
    bool    show_value = true;
    ValueStyle value_style = ValueStyle::Pill;   // Pill | Badge | Hidden
};
```

Render decision table (single source of truth, replacing the ad-hoc conditions in
`draw_nodes`):

| `show_label` | `show_value` | connected | rendered |
| --- | --- | --- | --- |
| yes | yes | no | `Asset` … pill `npc_elder` |
| yes | yes | yes | `Asset` … (no pill; the wire *is* the value) |
| no | yes | no | pill only, full row width available |
| yes | no | either | label only |

Which pins use `show_label=false`: any pin whose identity is already unambiguous from the
node — e.g. a component's single `ID [n]` output, and the object's `Pos`/`Scale` transform
pins (the label is redundant with the node's own position subtitle). This is a *presentation*
choice carried on the pin, decided at graph-build time by the backend, so the canvas stays
dumb.

For the migrated backends (`02-…` §6.2), the rule collapses to:

| Old | New |
| --- | --- |
| `name="Asset: npc_elder"`, `default_value="npc_elder"` | `name="Asset"`, `default_value="npc_elder"` |
| `name="Pos (919, 394)"`, `default_value="919.5, 394.5, -29.9"` | `name="Pos"`, `default_value="919.5, 394.5, -29.9"`, `show_label=false` |
| `name="Intensity: 0.30"`, `default_value="0.300000012"` | `name="Intensity"`, `default_value="0.3"` (format at build time, `%.3g`) |
| `name="Scale 1.0 · Rot 0.00"`, `default_value="Scale=1.0"` | split into two pins: `Scale` / `Rot`, each with its own value |

`Town_shop`-style `Intensity : 0.300000012` is a separate small fix: values are formatted
for display with `%.4g` at graph-build time so the pill shows `0.3`, not `0.300000012`. The
raw value stays in the tooltip and in the model.

---

## 4. Bug 4 — the stray "histogram" widget

### Root cause

It is **`draw_minimap()`**, and it is not dead scaffolding — it is a live, interactive
minimap with a real defect profile:

1. It is drawn as step 7 of `paintEvent`, **after `p.restore()`**, i.e. in widget space on
   top of everything, with `m_minimap_rect = QRect(width()-185, height()-130, 170, 115)`.
2. It is **always on**: `bool m_show_minimap = true;` (`graphy_canvas.h:137`) with **no
   toggle** — `grep -n m_show_minimap` finds only the paint call (line 296), the mouse hit
   test (line 720), and the default. There is no toolbar button and no key handler for it,
   so a user cannot dismiss it.
3. **Nothing is clipped.** There is no `setClipRect` in the file, so the minimap's own draws
   are not confined, and the node cards behind it are simply overdrawn with no affordance.
4. The rendering is `for each node: p.drawRect(QRectF(tl,br))` into a 170×115 box. At 85
   nodes that is 85 unlabelled rectangles whose heights vary with node height, laid out by
   world Y — **which reads exactly like a histogram or equalizer.** The user's description is
   a correct read of the pixels.

So the bug is: *an always-on, unclip-able, unlabelled, per-node-rect overlay that looks like
debug output.* All four properties are fixable, and the widget is worth keeping once fixed
(it is genuinely useful on an 85-node graph).

### Fix rules

> **Rule L8 — every overlay is (a) clipped to its own rect, (b) toggleable, (c) docked to an
> edge, and (d) aggregated rather than per-element.**

```cpp
void GraphyCanvas::draw_minimap(QPainter& p) {
    if (!m_minimap_visible || m_graph->nodes().size() < kMinimapMinNodes) return;
    const QRect r = minimap_rect();                 // honours m_minimap_dock
    p.save();
    p.setClipRect(r, Qt::IntersectClip);            // (a)
    …
    // (d) aggregate: one filled cell per kCell px of minimap space,
    //     not one rect per node. 85 nodes -> ~12x8 cells -> reads as a map.
    …
    p.restore();
}
```

Specific changes:

| Property | Change |
| --- | --- |
| visibility | `m_minimap_visible` (rename from `m_show_minimap`), default **false**; toolbar toggle + `M` in `keyPressEvent`; auto-shown only when `node_count >= 40` **and** the graph does not fit the viewport |
| docking | `enum class MinimapDock { BottomRight, BottomLeft, TopRight, TopLeft, Hidden }`; `minimap_rect()` derives the rect from the dock, so the marquee/pan code has one authority |
| clipping | `p.setClipRect(r, Qt::IntersectClip)` and a matching `setClipRect` guard around the node-drawing pass so the overlay cannot bleed |
| rendering | bucket nodes into a coarse grid (`kCell = 8` in minimap px) and fill one cell per bucket, alpha-blended by density. Draw the viewport frustum rect last, above the cells |
| interaction | keep the existing click/drag-to-pan (it is correct and useful), but only when the minimap is visible |

The `MinimapDock` change also fixes an interaction bug: today `mousePressEvent` treats a
click in `m_minimap_rect` as a minimap click *before* checking for node hit-testing, so the
bottom-right corner of the canvas is a dead zone for node interaction. Once the dock is
explicit, a hidden minimap (default) removes the dead zone entirely.

---

## 5. Additional bugs found while diagnosing (not in the brief)

These are real, reproducible, and cheap to fix alongside. Each has the same characteristic:
a hard-coded constant standing in for a computation.

### B5 — `frame_all()` cannot frame any real graph

```cpp
m_zoom = std::clamp(std::min(zx, zy), 0.25f, 1.35f);
m_pan_x = (width()  - gw * m_zoom) * 0.5f - min_x * m_zoom;
```

The `0.25f` lower clamp means a graph needing zoom < 0.25 is **not fitted** — `m_pan_x` is
computed for `m_zoom = 0.25`, so the bounds extend off-screen and `F` (Frame All) leaves most
nodes invisible. `town_elderhouse_graph.json` spans roughly 4 group columns × 750px plus
heights, i.e. it needs ~0.15. Fix: `kMinZoom = 0.05f`, and make `frame_all()` also account
for the docked minimap/HUD so "fit" means "fit in the *visible* area".

### B6 — layout is mutated inside `paintEvent`

```cpp
void GraphyCanvas::paintEvent(QPaintEvent*) {
    …
    if (m_graph) { for (auto& n : m_graph->nodes()) if (n) update_node_layout(*n); }
```

A paint handler must not mutate the model. Consequences: hit-testing on the frame *before* a
repaint sees stale geometry; `settle` cost is paid on every repaint even when nothing
changed; and it makes the canvas untestable (no way to ask for geometry without painting).
Fix: a `layout()` pass invoked from `set_graph`, `graph_modified`, `resizeEvent`, font/DPR
changes, and node moves, guarded by a `m_layout_dirty` flag. `paintEvent` becomes pure.

### B7 — two competing height authorities

`scene_to_graph.py` emits `height = max(90, 44 + rows*24)`; `update_node_layout()` computes
`32 + rows*22 + 8`. `paintEvent` uses `max(json_h, calc_h)`. So the canvas paints a card at
the *larger* of two formulas while laying pins out by the *other* one — 13 nodes in
`town_elderhouse_graph.json` carry a >8px dead band at the bottom. Fix: the JSON carries only
`x`/`y` hints; the canvas's layout engine is the single authority for both size and pin
positions (`02-…` §6.4).

### B8 — hit-testing does not match rendering

| Defect | Detail | Fix |
| --- | --- | --- |
| pins | drawn radius `5.0f`, hit radius `12.0f` | one `kPinRadius` + a separate `kPinHitSlop`, and use the *same* constant in both passes |
| wires | `hit_test_connection` samples the cubic at 24 steps and compares to a 7px tolerance | use `QPainterPathStroker` with `setWidth(2*tolerance)` and `path.intersects`/`contains`, i.e. exact, and the same helper used for drawing. Cite Unreal's `SplineHoverTolerance` + `MakeSplineReparamTable` (`08-…` §2.3): 24 uniform `t` samples are also *unevenly spaced along the curve*, so the effective hit tolerance varies along a wire |
| reroute knots | 24×24 box with a 12px centre, consistent | keep, but derive from one constant |

### B9 — the search palette and slice-line are drawn in mixed spaces

`draw_slice_line` is called before `p.restore()` (world space) while the marquee and minimap
are drawn after (widget space). That happens to be correct today, but there is no comment or
invariant enforcing it, and B6's refactor will move this code. Fix: name the two passes
`draw_world_pass()` / `draw_screen_pass()` and put *everything* in exactly one of them, with
an assertion in debug builds that no node/comment/wire geometry leaks into the screen pass.

---

## 6. The layout engine, as an API

```cpp
// src/ruby/graph/graphy_layout.h
namespace ruby::graph {

struct TextMetrics {                 // wraps QFontMetrics, cached
    int  advance(const QString&) const;
    int  height() const;
    int  lineHeight() const;
    QString elide(const QString&, int width, Qt::TextElideMode) const;
};

struct LayoutTheme {                 // every magic number lives here, once
    int pad_x = 12, pad_y = 6, gutter = 8;
    int min_node_w = 190, min_row_h = 22, pad_bot = 8;
    int port_column = 14;            // space reserved for pins on each side
    int pin_radius = 5, pin_hit_slop = 7;
    int pill_pad_x = 3, pill_pad_y = 2;
    int max_pill_w = 140;
    int header_gap = 2;
    QFont title_font, subtitle_font, pin_font, pill_font, comment_font;
};

struct RowLayout { int y, height, label_x, label_w, pill_x, pill_w; bool pill_visible; };
struct NodeLayout {
    QRectF card, header;
    QRectF title_rect, subtitle_rect;
    std::vector<RowLayout> input_rows, output_rows;
    int    width()  const;
    int    height() const;
    QPointF input_pin_pos(int index) const;
    QPointF output_pin_pos(int index) const;
};

class LayoutEngine {
public:
    explicit LayoutEngine(LayoutTheme t = {});
    void invalidate();                                    // font / DPR changed
    const NodeLayout& ensure(const Node&);                // compute-if-dirty, cached by node id
    void layout_graph(Graph&);                            // fills Pin::canvas_x/canvas_y
    void arrange_collision_free(Graph&);                  // GraphEditArranger analogue
private:
    LayoutTheme m_theme;
    TextMetrics m_title, m_sub, m_pin, m_pill;
    std::unordered_map<int, NodeLayout> m_cache;
    bool m_dirty = true;
};
}
```

Rules L1–L8 are implemented **inside** `LayoutEngine`. The canvas becomes:

```cpp
void GraphyCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    draw_grid(p);
    p.save(); p.translate(m_pan_x, m_pan_y); p.scale(m_zoom, m_zoom);
    draw_world_pass(p);          // comments, wires, active wire, nodes, slice line
    p.restore();
    draw_screen_pass(p);         // marquee, docked minimap, HUD
}

void GraphyCanvas::draw_world_pass(QPainter& p) {
    for (const auto& n : m_graph->nodes()) {
        const NodeLayout& L = m_layout.ensure(*n);
        draw_card(p, *n, L);
    }
}
```

`draw_card` reads only from `NodeLayout`, never computes geometry, never calls
`Qt::AlignVCenter` into a shared band, and never draws text without an explicit elide-or-fit
decision.

---

## 7. Regression tests

The probe in `.scratch/graphy_recon/graphy_layout_probe.py` becomes a **real test**,
`tests/graphy_layout_test.cpp`, run in CI. It must assert, for every committed
`*_graph.json` plus synthesized worst cases:

| # | Assertion | Reported bug |
| --- | --- | --- |
| G1 | for every node: `header.height() >= title_rect.height() + gap + subtitle_rect.height() + 2*pad_y` and `title_rect` and `subtitle_rect` do not intersect | Bug 1 |
| G2 | for every pin row: `label_rect.right() + gutter <= pill_rect.left()` | Bug 2 (label/pill overlap) |
| G3 | for every rendered pill: `fm.advance(shown) <= pill_rect.width() - 2*pad_x` | Bug 2 (truncation) |
| G4 | for every pin: `!(name.contains(default_value) && default_value.length() > 3)` | Bug 3 (duplicated value) |
| G5 | with `minimap_visible = false`, no paint call touches the minimap rect; with it true, the clip rect is set before any cell is drawn | Bug 4 |
| G6 | `frame_all()` on each fixture produces a pan/zoom for which **every** node rect is inside the widget rect | B5 |
| G7 | `layout()` is idempotent: calling it twice changes no `Pin::canvas_*` and no node size | B6 |
| G8 | for every node: `card.height() == header + Σ row heights + pad_bot` (one authority) | B7 |
| G9 | `hit_test_pin(hit_test_connection(...))` round-trips for a sampled point on each drawn wire | B8 |
| G10 | worst case: a node whose subtitle is 120 chars, whose value is a 40-char vector, and whose title is 60 chars → no rect intersection, all pills elided, node width `<= 320` | all |

G3 and G4 are the two that directly encode the reported symptoms; G10 is the one that stops
a future regression from reintroducing them.

**Golden-image test:** render `town_elderhouse_graph.json` at `dpr = 1` and `dpr = 2` and
assert **no text pixels are clipped** by comparing the rendered text extents against the
layout rects. This is the test that would have caught the HiDPI variant.

---

## 8. What not to do

- **Do not add `+N` pixel nudges.** The current bug *is* a `+14`. Rules L1/L4 replace
  constants with measurements; another constant is a regression waiting to happen.
- **Do not fix `scene_to_graph.py` alone.** The duplicate value tag is a data bug, but the
  renderer must also be unable to express it (Rule L7), or the next backend will reintroduce
  it. Fix both; the *invariant* (G4) is what protects us.
- **Do not delete the minimap.** It is useful on an 85-node graph; it is only
  under-engineered (Rules L8). Deleting it trades one bug for a usability regression.
- **Do not re-layout inside `paintEvent`.** Even after Rules L1/L4 make geometry cheap, B6
  stays a correctness bug (stale hit-testing) and a testability blocker.
