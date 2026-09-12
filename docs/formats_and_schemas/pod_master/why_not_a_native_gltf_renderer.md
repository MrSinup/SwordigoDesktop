# Why does `ruby` convert glTF → POD just to *view* it? Can it render glTF natively?

> **Question:** Why is even *viewing* a `.glb` routed through POD? Why isn't there a native, full-featured glTF renderer in `ruby`?
>
> **Verified by reading:** `src/tools/asset_viewer.cpp` (18,123 lines), `src/tools/av_renderer.{h,cpp}`, `src/tools/gltf_import.cpp`, `src/tools/pod_loader.h`.

---

## 1. Short answer

**Nothing in the GPU renderer requires POD.** The renderer is already format-agnostic — it uploads raw float arrays and draws them. glTF is funnelled through `PODModel` today **only because `PODModel` is being used as the viewer's generic in-memory mesh container**, and the two animation helpers (`get_node_matrix`, `skin_mesh`) happen to be written against the `PODModel` struct. A native glTF render path is entirely feasible and mostly already there; what's missing is a glTF-native animation/skin sampler, not a renderer.

---

## 2. What the evidence shows

### 2.1 The renderer is NOT POD-native
`av_renderer.h` — the GPU layer — has **no POD types at all**. Its API is raw arrays:

```c
GPUMesh upload_mesh(const float* positions, const float* normals, const float* uvs,
                    /* indices, counts … */);
void    render_mesh   (const GPUMesh&, const float* model_matrix, …);
void    pbr_render_mesh(const GPUMesh&, const float* model_matrix, …);
```
The header comment even says it builds *"VBO/VAO/EBO from PODMesh-**style** arrays"* — **style**, i.e. plain `float*`, not the POD type. `grep` for `tg3_model`/`PODModel` in `av_renderer.h` → **zero hits**. The GPU path does not know or care what format the mesh came from.

### 2.2 glTF is already parsed natively — then flattened into POD
`asset_viewer.cpp:2268` loads a `.glb` with `gltf_import_glb(...)` (the `tg3` parser) straight into an `av::PODModel`, and even carries **glTF PBR materials** (`gltf_pbr_materials`, alpha mode/cutoff at l.2325). So glTF is read with full fidelity — and then **down-converted into `PODModel`** purely to reuse the viewer's existing mesh/upload/animation plumbing.

### 2.3 The ONLY place POD semantics leak into rendering
The draw loop animates/skins via two `PODModel`-typed helpers:
- `av::get_node_matrix(st.model, node, frame, out)` (asset_viewer.cpp:3195/3244/9885/10786)
- `av::skin_mesh(st.model, node, frame, positions, normals)` (l.3265/9902/10801)

These read `PODModel` fields (`anim_translation/rotation/scale`, `bone_batches`, `num_frames`). Everything downstream — `upload_mesh` → `pbr_render_mesh` — is generic. **So "must convert to POD to view" reduces to "our animation sampler is written against the POD struct."**

---

## 3. Why it ended up this way (not a technical necessity)

- **The viewer was born as a POD asset tool.** Its whole job was previewing/editing stock game `.POD` files, so `PODModel` became the one in-memory model type. glTF/OBJ/.scn support was **bolted on by adapting them into `PODModel`** so they'd flow through the existing upload + animation + UI code (bounding box, feet offset, frame scrubber, material panel — all keyed on `PODModel`).
- **Reuse, not a rendering constraint.** Converting to POD let glTF instantly inherit the scrubber, skin path, PBR panel, thumbnails, scene cache (`scene_model_cache` is `map<string, PODModel>`). It was the cheapest way to get glTF on screen — at the cost of POD's lossiness (single UV set, no morph targets, 7-float scale quirk, byte-limited bones).

**Consequence:** viewing a glTF inherits every POD limitation and every glTF→POD conversion bug (see `glb_to_pod_animation_conversion.md`) — even though the screen never needed POD.

---

## 4. Can there be a native full-featured glTF renderer? — Yes

The building blocks already exist; only the animation/skin layer is POD-bound. Two viable strategies:

### Strategy A — Generic render model (recommended)
Introduce a small **format-neutral runtime type** the renderer + UI consume, and make POD *and* glTF both *sources* for it (not one wrapping the other):

```
.POD  ──pod_loader──▶ ┐
                       ├──▶  RenderModel  ──▶ upload_mesh / pbr_render_mesh (unchanged)
.glb  ──tg3_parse ──▶ ┘        ▲
                               └── AnimSampler (interface): PodAnimSampler | GltfAnimSampler
```

- `RenderModel` = meshes (float arrays already in the renderer's layout) + node tree + a **`AnimSampler` interface** with `node_matrix(node, time)` and `skin(node, time)`.
- `PodAnimSampler` wraps today's `get_node_matrix`/`skin_mesh` (POD path unchanged — zero regression risk).
- `GltfAnimSampler` samples the **`tg3_animation` directly**: real seconds-based time, native slerp, sparse keys, **no 7-float-scale hack, no byte-bone cap, no joint-remap loss.** glTF renders at full fidelity.
- The UI (scrubber, PBR panel, bbox) binds to `RenderModel`, not `PODModel`.

**Result:** glTF is viewed natively; POD is viewed natively; conversion becomes an *explicit export action*, not a hidden prerequisite for display.

### Strategy B — Minimal: a glTF-native sampler behind the current calls
If a full `RenderModel` refactor is too big now, just add a `GltfAnimSampler` and branch the two call sites (`get_node_matrix`/`skin_mesh`) on model source. Smaller change, keeps `PODModel` as the mesh holder, but still removes the animation-fidelity loss for viewing.

### What "full-featured" unlocks (that POD viewing can't do today)
- Multiple UV sets (glTF `TEXCOORD_1` — POD keeps one), morph targets, full PBR metal-rough/normal/occlusion/emissive, cameras/lights (KHR_lights_punctual), >255 bones, seconds-accurate animation with the source's own interpolation. All already present in `tg3_model`; all lost in the POD funnel.

---

## 5. When you SHOULD still go through POD

Converting to POD remains correct for its real purpose: **producing a game-loadable asset.** The game engine only loads `.POD`, so `--glb2pod` must exist and must be accurate (that's what `glb_to_pod_animation_conversion.md` fixes). The point of this document is narrower:

> **Viewing** a glTF should use a glTF-native path; **shipping** a glTF to the game should use the (fixed) POD exporter. Today the viewer conflates the two by always converting.

---

## 6. Recommendation

1. **Decouple rendering from POD** via a `RenderModel` + `AnimSampler` interface (Strategy A). The GPU renderer already supports this — no shader/upload changes needed.
2. **Add `GltfAnimSampler`** over `tg3_animation` so previews are fidelity-accurate and independent of the POD exporter's bugs.
3. **Keep `--glb2pod` as an explicit export**, wired to the fixed conversion + the `gltf_bridge` round-trip module in `gltf_library_comparison_and_roundtrip_architecture.md`.
4. Net effect: a **native full-featured glTF renderer** in `ruby`, POD conversion demoted from "always, invisibly" to "only when exporting to the game."

---

## 7. One-paragraph summary

`ruby`'s renderer (`av_renderer`) is already format-agnostic — it draws raw float arrays, not POD. glTF is converted to `PODModel` for *viewing* only because `PODModel` is the viewer's universal mesh container and the animation helpers (`get_node_matrix`/`skin_mesh`) are written against it — an organic-growth artifact, not a rendering requirement. A native, full-featured glTF renderer is very achievable: introduce a format-neutral `RenderModel` fed by an `AnimSampler` interface, add a `GltfAnimSampler` that samples `tg3_animation` directly, and keep POD conversion strictly as an explicit "export to game" step.
