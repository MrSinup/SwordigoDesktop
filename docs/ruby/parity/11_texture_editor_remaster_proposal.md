# 11 — Texture Editor Remaster: Toolbar Rebuild + Full Editor Proposal

Status: Toolbar **implemented** in `src/tools/asset_viewer.cpp :: draw_texture_preview()`.
Remaster proposal below is a **written design deliverable** with a prioritized build order.

---

## Part 1 — The Rebuilt Toolbar (implemented)

### Problems with the old toolbar
The old strip was one undifferentiated row —
`Edit | Brush | Eraser | Fill | Pick Crop | ? Bake | Save | Reload | 1px | 41 | 140 | 255 | 255 | [swatch] Color(original)` —
mixing five different *kinds* of control with only thin `|` glyphs for structure:

1. a **mode toggle** (`Edit`),
2. **tool selection** (Brush/Eraser/Fill/Pick/Crop) drawn as loose `Selectable`s with no clear pressed state,
3. **one-shot actions** (Bake/Save/Reload) styled the same as persistent tools,
4. **orphaned numeric state** (`1 px` brush size sat right next to the RGBA numbers with no separation, so it read as a fifth color channel),
5. a **swatch + `(original)` label** whose relationship to the numbers and whose *meaning* were both ambiguous.

Additional defects: inconsistent icon/label conventions (some icon-only, some text-only like `Save`/`Reload`); a stray `?` prefix on `Bake`; long tooltip sentences that dropped a big opaque box over the canvas; no R/G/B/A labels; and no discoverable channel/alpha view.

### The rebuild — section structure
The toolbar is now a **grouped strip** separated by real vertical rules (`tb_sep()` draws a 1px rule via the window draw list, since `ImGui::SeparatorEx` is internal) and an 8px spacing rhythm. Left → right:

| Section | Contents | Notes |
|---|---|---|
| **Mode** | `Edit` checkbox (or `Convert…` when not editing) | unchanged behavior |
| **Tool palette** | Brush · Eraser · Fill · Pick · Crop | proper `Button` group; **active tool gets a solid accent background** (`g_theme.accent` + contrast-safe text via `th_text_on`) so the selected tool is unmistakable; every tool is **icon + label** (one convention) |
| **Tool options** | contextual to active tool | Brush/Eraser → labeled `Brush` size slider (or `1 px` when pixel-map); Fill/Pick → short hint; Crop → Apply/Cancel when a rect is ready |
| **Color** | `Paint color` label + swatch + picker + `R# G# B# A#` 0–255 readout | swatch opens the full ImGui color picker popup; the RGBA readout makes channel values explicit without four unlabeled boxes |
| **View** | `RGBA · RGB · A` 3-way toggle | new `tex_view_channel` state; drives the canvas draw (see below) |
| **File actions** | Bake · Save · Undo · Reload | grouped after a rule so one-shots read differently from tools; Undo is **always present but disabled** when the undo stack is empty (discoverability) |
| **Status** | `Modified` / `Original` | pinned to the far right, styled as passive status (this is a **dirty-state indicator**, not a swatch or action — the old `Color(original)` ambiguity is resolved) |

### Tooltips (canvas-safe)
`tb_tip()` anchors each tooltip's **bottom-left to the item's top-left** (`SetNextWindowPos(itemMin, pivot=(0,1))`) so the box stays inside the toolbar band and never drops over the working canvas, and every tip is a single short phrase ("Paint", "Erase", "Flood fill", …).

### On-canvas zoom HUD
A translucent bottom-left overlay on the canvas shows the live `NN%` zoom and three preset buttons:
- **Fit** — fit image to window (reuses the existing reset path),
- **100%** — actual size, pan reset,
- **1:1** — pixel-perfect (snaps to the nearest integer zoom for crisp texels).

### View-channel behavior on the canvas
`tex_view_channel` changes how the image is composited:
- **RGBA** (0): checkerboard shows through transparent pixels (unchanged).
- **RGB** (1): solid black backdrop, image drawn with alpha forced opaque → color inspected without transparency.
- **A** (2): black backdrop + white-tinted image as an alpha coverage matte.
  > *Limitation:* `ImDrawList::AddImageQuad` multiplies the texel by a tint, so a true single-channel alpha-as-luminance readout needs a dedicated GL shader (see P2 below). The current matte is an approximation; the proper isolated-alpha view is the first shader task in the remaster.

---

## Part 2 — Full Editor Remaster Proposal (prioritized)

### P0 — Ship-blockers / already done
- [x] Grouped toolbar with active-tool state, consistent icon+label, canvas-safe tooltips.
- [x] Visible zoom readout + Fit / 100% / 1:1 presets on the canvas.
- [x] Discoverable Undo affordance (always shown, disabled when empty).
- [x] Labeled color + RGBA readout + full picker.
- [x] RGB / RGBA / Alpha **view toggle** wired into state and the canvas composite.

### P1 — High value, low risk
1. **True alpha-channel view (shader).** Add a tiny GL program that samples the texture and writes `vec3(a)` (or a red-matte) so the `A` view is an exact single-channel readout rather than the AddImageQuad approximation. This is the correct home for the `tex_view_channel==2` path and unblocks real alpha editing for ETC1/PVRTC + alpha textures.
2. **Discoverable multi-step undo/redo.** The editor currently keeps a single-step `tex_edit_undo` snapshot. Promote to a bounded stack (match the mesh editor's "N steps"), expose **Undo** *and* **Redo** buttons in the File-actions group with a step counter tooltip, and bind Ctrl+Z / Ctrl+Shift+Z.
3. **Brush preview parity across tools.** The brush ring only draws for Brush/Eraser; show a 1-px crosshair for Pick and a marquee cursor for Crop so the active tool's effect is always previewed on the canvas.

### P2 — Professional editing features
4. **Rectangle/lasso selection + masking.** Add a Select tool so Fill/Erase/tint can be constrained to a region (reuse the Crop rect-drag interaction as the rectangle-select foundation; lasso is a later add). This is the biggest "feels like a real editor" gap.
5. **Non-destructive Inspector operations preview.** The Inspector's Texture Operations (rotate / flip / color-tint) should preview live on the canvas *before* Bake/Save commits them, with a clear "Bake to apply" affordance — mirroring how `texture_tint` already previews via the draw tint but rotate/flip currently only apply on Bake.
6. **Layer/channel panel.** A compact side strip: per-channel visibility toggles (R/G/B/A eyes), and a premultiplied-alpha warning for container `.tex` formats.

### P3 — Consistency & polish
7. **Unify with the app-wide Inspector/toolbar conventions.** Route the Texture Editor's swatches/sliders through the shared `av_color_edit4` / `av_slider_float` / `av_text_elided` helpers already used elsewhere so it doesn't feel like a bolted-on panel.
8. **Save UX.** Remember the last save target per source file, show the resolved output path inline, and warn before Overwrite Source on container formats.
9. **Keyboard shortcuts.** B/E/F/I/C for the five tools, `[` / `]` for brush size, `0` = Fit, `1` = 100%.

### Suggested build order
**P1.1 (alpha shader) → P1.2 (undo/redo stack) → P2.4 (selection) → P2.5 (live preview) → P3 polish.**
Rationale: the alpha shader closes the one honesty gap in what already shipped; the undo/redo stack is the highest-frequency editing pain; selection is the single feature that most changes the editor's ceiling; live preview + polish are quality-of-life once the core is solid.
