# Ruby Swordigo Studio — GUI Remaster Proposal

> Separate deliverable. This is a **written redesign proposal**, not code. It
> assumes the visual-bug sweep (see the "Bug sweep" section at the end) is
> already merged. Prioritized so the highest-impact, lowest-risk work lands
> first.

---

## 0. Context & principles

Ruby is a Dear ImGui immediate-mode desktop editor for Swordigo assets (POD
models, `.scene`, `.fnt`, `.pvr`). Today it uses **fixed-position side panels**
(Files left, Inspector right), a **flat neutral dark theme**, and each tab lays
out its own Inspector controls ad-hoc. That ad-hoc layout is the root of most of
the visual bugs (label clipping, controls bleeding across panels), so the
remaster is as much about *structure* as *looks*.

Guiding principles:

1. **One layout system, reused everywhere.** No tab should hand-roll label +
   control geometry. (The bug sweep already introduced shared `av_*` Inspector
   helpers — the remaster generalizes them into a real framework.)
2. **Space is reserved before it's consumed.** Toolbars pin their controls
   first, then variable-length content (paths, names) elides into what's left.
3. **Nothing escapes its panel's clip rect.** Every widget is bounded by the
   child window it lives in.
4. **Transient vs. persistent information are visually separated.**

---

## 1. Prioritized roadmap (build in this order)

### Tier 1 — Highest impact, do first (structural, unblocks everything else)

**1.1 A single reusable Inspector framework.**
Generalize the `av_labeled_item_width` / `av_color_edit*` / `av_slider_float` /
`av_drag_float` / `av_text_elided` helpers (added during the bug sweep) into a
proper `InspectorRow` abstraction:

- `insp::Row("Label")` sets a **consistent min label-column width** (e.g. 40% of
  panel width, clamped), draws the label on the left, tooltip-on-truncation
  built in, then places the control in the remaining width.
- Provide `insp::Color`, `insp::Slider`, `insp::Drag`, `insp::Combo`,
  `insp::Text`, `insp::Toggle` that all consume the same row geometry.
- Convert every Inspector tab (File, Scene, Mesh, Texture, Model/Lighting) to
  build from it.

*Why first:* it permanently kills the label-clipping / control-bleed class of
bugs and makes every later visual change one-line-per-widget. Everything below
is easier once this exists.

**1.2 Dockable / resizable panel system (ImGui docking branch).**
Move Files/Inspector/Viewport/Status from hand-positioned splitters to
`ImGuiConfigFlags_DockingEnable` with a saved default layout. Panels become
rearrangeable, floatable, and collapsible per task (e.g. hide the Inspector while
tracing a mesh; float the Files browser on a second monitor). Persist the dock
layout in the existing settings/ini.

*Why:* the toolbar rows in Scene view (Select/Move/Rotate/Scale/Mesh Edit/Snap/
Gizmo/Mode/Frame) are horizontally scroll-clipped because the viewport can't be
widened independently. Docking fixes the whole cramped-toolbar problem instead of
patching it.

### Tier 2 — High impact, medium effort

**2.1 Asset browser usability overhaul.**
With hundreds of `achicon_*`, `boss1_*`, `florennum_*`, `fire_part*`,
`font_megalopolis_*` files, the flat icon+name list is the biggest daily
friction. Add, in priority order:

1. **Fuzzy search** (subsequence match + score) replacing/augmenting the
   All/Textures/Models/Sc… filter tabs. Highest ROI.
2. **Thumbnail previews** for textures (already decodable) and a small rendered
   POD thumbnail for models, cached to disk by content hash.
3. **Collapsible prefix grouping** — auto-group by the common `prefix_` token so
   `achicon_*` collapses into one header.
4. **"Recently opened"** section pinned at the top (reuse the existing
   file-event log).

**2.2 Mesh editor UX (beyond the alignment fix).**
The GMG panel's alignment bug is fixed in the sweep (ortho reference camera). The
UX upgrades:

- **Reference overlay opacity slider** (0–100%) so artists dial how strongly the
  background silhouette shows through the point polygon. (Today it's a fixed
  ~58% tint.)
- **Split view toggle**: reference silhouette + live 3D preview side-by-side, or
  overlay — user's choice.
- **On-canvas ruler / scale overlay** and a **"recalibrate reference"** action
  that re-frames and re-asserts the shared transform, giving a one-click way to
  *verify* alignment against a known landmark.

### Tier 3 — Polish, do last

**3.1 Status bar split.** The status bar currently double-duties transient load
messages ("Loaded ballofgold.POD — 1 mesh(es)…") and persistent hotkey hints
("LMB orbit RMB pan…"). Split into two regions: a **left transient message area**
(auto-fades) and a **right persistent shortcut-hint area** that's
context-sensitive to the active tool.

**3.2 Theme & density pass.** Keep the dark base, but:
- Raise contrast between **selected vs. unselected** file rows and **active vs.
  inactive** tabs (they read too similarly today).
- Adopt a tightened, consistent spacing/type scale (e.g. 4/8/12/16 px steps; one
  header size, one body size, one small/caption size).
- Define semantic color tokens (already started with `th_*` helpers) and route
  all ad-hoc RGB literals through them.

---

## 2. Concrete "consistent Inspector framework" sketch

```
namespace insp {
  // Begins a labeled row: reserves a consistent label column, returns the
  // control width to use for the trailing widget. Tooltip-on-truncation is
  // automatic for the label.
  float BeginRow(const char* label);   // returns control width
  // Convenience wrappers that call BeginRow + SetNextItemWidth internally:
  bool  Color(const char* label, float* rgba, int comps);
  bool  Slider(const char* label, float* v, float lo, float hi, const char* fmt);
  bool  Drag(const char* label, float* v, float speed, float lo, float hi);
  bool  Combo(const char* label, int* idx, const char* items);
  void  Text(const char* label, const char* value);   // value elided + tooltip
}
```

Rules the framework enforces (so bugs can't recur):
- Label column width = `clamp(0.42 * panelWidth, 90px, 180px)`.
- Control width = `panelWidth - labelCol - spacing`, never negative.
- Everything drawn inside the current child's clip rect.
- Any text wider than its cell auto-truncates with `…` + hover tooltip.

---

## 3. Regression guards to add alongside the remaster

- A **debug overlay** (toggle) that draws each Inspector panel's clip rect and
  flags any widget whose item rect exceeds it — catches "bleed" bugs at a glance.
- A **fixed test scene + mesh** checked into `docs/ruby_parity/test_assets/` so
  the GMG alignment, breadcrumb, and truncation behaviors can be eyeballed at
  several zoom / pan / window-size states before release.
- Assertions in `insp::BeginRow` that control width stays positive.

---

## Appendix — Visual bug sweep (implemented, for reference)

Root-caused and fixed in code (see `08_transform_gizmo_parity.md` for the gizmo
work and the source files for the rest):

| # | Bug | Root cause | Fix |
|---|-----|-----------|-----|
| **P0** | **GMG mesh-editor background silhouette doesn't line up with the point polygon** | The point polygon uses a pure orthographic `to_screen` map (`px = world * gm_canvas_scale`), but the reference mesh was rendered through a **perspective** FBO camera. Perspective foreshortening scaled the extruded front/back faces (`gm_min_depth..gm_max_depth`) differently from the flat Z=0 outline → stretched/skewed silhouette. | Switched the reference camera to **orthographic** (`pc.orthographic = true`, `ortho_zoom = 1`), reusing the same `distance = ch*0.5 / (scale*tan(fov/2))` so the ortho half-height maps pixel-for-pixel onto `to_screen` at every zoom/pan. Near/far planes widened to straddle the full extrusion depth. `asset_viewer.cpp`, GMG canvas block. |
| 1 | Inspector labels clipped ("Key Light Col", "Ambient (Gro") | `ColorEdit3`/`SliderFloat` drawn at full item width pushed their right-side labels past the narrow panel edge. | Added shared `av_color_edit3/4`, `av_slider_float`, `av_drag_float`, `av_labeled_item_width` helpers that size the control to leave room for the label; applied to Interactive Lighting. |
| 2 | Font/text editor path overlaps zoom + Copy All | Path printed in full via `TextDisabled` on the same line; controls pinned at a fixed right offset, so long paths ran under them. | Reserve the right controls region first, then left-elide the path (keeping the filename) into the remaining space with a full-path tooltip. `intellij.cpp` toolbar. |
| 3 | Texture editor Color Tint RGBA fields bleed into file list | `ColorEdit4` full-width overflowed the Inspector's right edge; its inline inputs aren't clipped to the child. | Routed through `av_color_edit4` so it fits inside the Inspector. |
| 4 | File browser long filenames hard-cut, no ellipsis/tooltip | `Selectable` clips with no `…` and no tooltip. | Added hover tooltip showing the full filename when the row is truncated. |
| 5 | Scene viewport breadcrumb has a bare `/` segment | Path components with an empty `filename()` (trailing separator / root) were substituted with a literal `"/"` chip. | Skip empty-filename components entirely; the root is represented by the "Vanilla" shortcut + hidden-prefix collapse. |
| 7 | Inconsistent numeric widgets across tabs | Each tab picked slider/drag/plain text ad-hoc. | Introduced the shared `av_slider_float`/`av_drag_float` helpers as the single consistent path (full rollout tracked as Tier 1.1 above). |

> Bug #6 (ground-mesh rendering in the Scene *Visualizer* Visual tab looking
> disconnected from the background terrain) is intentionally **out of scope of
> the GMG-panel fix** per the reported clarification that the alignment defect is
> GMG-panel-only. It is logged here as a separate rendering investigation
> (texture bind / blend / mipmap) for a future pass, not bundled with the GMG
> transform fix.
