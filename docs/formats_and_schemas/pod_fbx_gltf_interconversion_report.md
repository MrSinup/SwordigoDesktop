# POD ↔ FBX ↔ glTF ↔ .scene Interconversion — Deep Research & Parity Report

> Scope: Swordigo game runtime (Caver engine, OpenSwordigo IDA decompile: `arm32` / `arm32_13` / `arm64_13`),
> the ruby (ImGui) editor (`src/`), the converters (`src/tools/pod_convert.*`, `fbx_import.*`,
> `gltf_import.cpp`, `gltf_export.cpp`, `pod_writer.cpp`, `pod_loader.*`), and the game's real
> asset corpus (`/home/quantumcreeper/resources/`).
>
> Task stage 1 — research. Every claim below is either verified by direct experiment (round-trips,
> measurement, conversion runs) or cited to a decompiled function / source line. No guesses.

---

## 0. Executive summary

The root causes of "I can't guess the size my POD will be in Swordigo" are four concrete, fixable defects:

1. **The ruby convert-modal's "auto-fit" presets are based on false reference numbers.**
   `asset_viewer.cpp` uses `Hero/NPC ~20u` (`s_hero = 20/cur_h`) and `Statue/Door ~62u`
   (`s_statue = 62/cur_h`). Measured stock assets: hero `ash.POD` is **74.2u tall**,
   `knight.POD` is **61.4u**, `dragonkin_statue.POD` is **100.6u**. Every preset therefore
   recommends a scale that produces midget models.

2. **FBX converter normalizes units to meters silently.** `fbx_import.cpp`
   sets `ufbx_load_opts.target_unit_meters = 1.0`. An FBX authored in centimeters
   (3ds Max/Maya default) is divided by **100** at load. The user then has no way to know the
   produced POD is "meters", and the "magic" scale factor (~40 for characters) is never surfaced.

3. **glTF converter double-scales when the root node has a non-identity transform.**
   `gltf_import.cpp` multiplies **mesh positions** by `opts.scale` *and* **multiplies the root
   node's matrix/TRS scale+translation** by `opts.scale`. A model whose glTF root node carries a
   scale (unit conversion, imported assemblies) ends up scaled to the square.

4. **FBX UV V is double-flipped for left-handed sources.** ufbx already flips V when it mirrors
   a left-handed scene (3ds Max / Unity exports, `model.uv_v_flipped == true`),
   but `fbx_to_pod` flips V again unconditionally → textures are upside-down on those models.

Plus a set of parity gaps between the ruby visualiser and the game (LocalAABB staleness after
scale edits, the game's `scale < 0.01` cull, display-vs-scene scale confusion, template_scaling
double-application) and a handful of writer/loader edge cases (ETC1 alpha loss, node `num_frames=0`
static-transform encoding, 1002/1003 chunks omitted — safe, etc.).

The good news, verified by experiment:

- **POD round-trip is bit-exact.** `knight.POD` → `pod_load` → `pod_write` → `pod_load` reproduces
  every position / normal / UV / index **with max diff 0.0** (`/tmp/pod_diff`).
- **GLB → POD is size-exact.** `MEGA/statue.glb` → `bin/ruby --glb2pod --scale 1.0` produces a POD
  with the **same bbox to 3 decimals** as the shipped `statue.POD` / `statue_knight.POD`.
---

## 1. Ground truth: how the game actually renders a model

All from OpenSwordigo `arm64_13` decompiles (IDA/Hex-Rays) of the same build family as the
converted PoCs; cross-referenced with the shipped `resources/` corpus.

### 1.1 Load path — `Caver::PODLoader`

```
ModelLibrary::ModelForName                    .../Caver/ModelLibrary/00000000004E283C__ModelForName.c
  ├─ PODLoader::PODLoader()                   .../PODLoader/00000000004E3C80__PODLoader.c
  ├─ PODLoader::ReadModelFromFile()           .../PODLoader/00000000004E3D64__ReadModelFromFile.c
  │    └─ CPVRTModelPOD::ReadFromMemory()     (PowerVR SDK legacy reader — accepts our chunks)
  └─ PODLoader::CreateModel()                 .../PODLoader/00000000004E3F0C__CreateModel.c
       • for every mesh node with a name starting "Bone": CreateSkeleton()
       • for every mesh: CreateMesh() then
            world = GetWorldMatrix(node)
            world = Scale(PODLoader[+124]) * world      // uniform load-scale, default 1.0
            Mesh::TransformVertices(world)  → geometry is BAKED in at load time
            Mesh::NormalizeNormals()
```

Key facts pinned from the decompile:

- `PODLoader` ctor zeroes the object; bytes `112..127` (= `*((_OWORD*)this+7) = xmmword_259F30`)
  fill offset **124** (a scale float, default 1.0) and byte **128** (a bool, =1).
- `CreateModel` reads that scale at `*((float*)this + 31)` (offset 124) and puts it on the
  diagonal of the matrix that premultiplies `GetWorldMatrix`. **No setter for offset 124 was found**
  in `ModelLibrary`, `PODLoader`, or component code → the engine does **not** re-scale models on
  load. **What your POD's vertex coordinates say is exactly what the game renders.**
- `Caver::Mesh::TransformVertices` + `NormalizeNormals` are called after baking — so node world
  transforms (including static/animated TRS and matrices) are *flattened into CPU vertex buffers*
  at load; the GPU draw path multiplies only the scene-object matrix.

### 1.2 Node transform — `CPVRTModelPOD::GetWorldMatrixNoCache`

```
.../CPVRTModelPOD/0000000000584794__GetWorldMatrixNoCache.c
```

Composes, in order:
1. scale   (`GetScalingMatrix` — static `pfScale` or the 5007 anim-scale array),
2. rotation (static quaternion `pfRotation` / 5005, or 5008 anim arrays; sparse anim via 5013/5014
   index tables + slerp),
3. translation (static `pfPosition` / 5004, or 5007 anim arrays),
4. full static matrix path (5010/5011) supported,
5. **recursive multiply by parent world matrix** (`nParentIdx` at node+20), so the entire POD
   hierarchy is honored.

→ **Conclusion:** both static TRS (5004/5005/5006), animation-encoded TRS (5007/8/9) and static
matrices (5010) written by our `pod_writer.cpp` are understood by the game reader. `get_node_matrix()`
in `pod_loader.cpp` implements the same algorithm for ruby, so per-node transforms match.

### 1.3 Scene-object transform — `SceneObject::WorldMatrix` / `TransformComponent::WorldMatrix`

- `SceneObject::WorldMatrix` (`.../SceneObject/0000000000503F28__WorldMatrix.c`):
  `M = RotationZ(angle@+144) · Scale(f@+156)` then `PreTranslate(pos@+128,132,136)`,
  i.e. **`M = T(pos) · Rz(rot) · S(scale)`**.
- `TransformComponent::WorldMatrix` (`.../TransformComponent/00000000003474D4__WorldMatrix.c`):
  `M = S(SceneObject@+156) · (component matrix) · PreTranslate(...)`. The scale pre-multiplies the
  component's own matrix.
- `ModelComponent::Draw` enables `GL_RESCALE_NORMAL` whenever
  `fabsf(SceneObject@+156 − 1.0) > 0.001` — **the scene field is the "SceneObject Scaling" (Tag 7).**

### 1.4 Culling / visibility — `Scene::DrawModels`

```
.../Caver/Scene/00000000004F8E80__DrawModels.c
```

An object is drawn only if **all** of:
1. `(hidden@+164 != 0) XOR a5` is 0 (visibility flag pass),
2. `(a4&1)==0 || *(float*)(obj+160) >= 0.01f || byte@+317` — **objects whose rendering-scale
   field (+160) is below 0.01 are skipped** (a real low-scale cull),
3. `Rectangle::IntersectsWithRect(ctx_rect(obj+180), view_rect)` — 2D-rectangle cull against the
   model component's AABB rectangle.

`ModelComponent::UpdateBounds` (`.../ModelComponent/0000000000332DA4__UpdateBounds.c`) recomputes
the model AABB from *skin-moved vertices* every bounds update, then
`Component::UpdateObjectBounds` → `SceneObject::UpdateBounds` unions all component AABBs. So for
3D model components the game computes bounds **from geometry**, not from the persisted scene
LocalAABB.

---

## 2. Ground-truth scale conventions (measured corpus)

`pod_stats` = custom build of `pod_loader.cpp`; all numbers from shipped `resources/*.POD`.

| Class | Model | Size W×H×D (units) | Notes |
|---|---|---|---|
| Hero | `ash.POD` | 79×74×36 | hero (main char) |
| Enemy | `knight.POD` | 70×61×22 | |
| Enemy | `grasswalker.POD` | 74×50×79 | |
| Enemy | `iceboss.POD` | 93×98×76 | |
| Boss | `endboss.POD` | 73×152×244 | |
| Boss | `boss1_shadowform.POD` | 244×114×190 | |
| Prop | `chest.POD` | 26×27×13 | |
| Prop | `bomb.POD` | Ø31 | radius ≈15.5 |
| Prop | `house_door.POD` | 120×103×17 | |
| Prop | `dragonkin_statue.POD` | 66×101×173 | "statue" prop |
| Prop | `fountain.POD` | 320×247×320 | |
| Prop | `signpost_blank.POD` | 85×87×13 | |
| Decor | `grass_tree1.POD` | 250×459×234 | |
| Decor | `rock1.POD` / `hugerock1.POD` | 110×134×78 / 656×156×125 | |
| Decor | `grove_torch.POD` | 797×817×1696 | huge prop |
| Boss | `statue.POD` (MEGA) | 927×976×6514 | boss statue arch |

**Unit system inference.** Ash is ~74u tall. Hero heights in the game behave like a ~1.8 m human →
**1 Swordigo unit ≈ 1 inch ≈ 0.0254 m; ≈ 39.4 units per meter.** Cross-check: house door 103u ≈
2.6 m (typical door), bomb Ø31u ≈ 0.79 m (plausible for a bomb prop), chest 27u ≈ 0.69 m. This gives
a working rule: **meters × 39.37 ≈ game units**, character-target heights ≈ **65–75u**.

There is **no single "model unit"** in the game — every stock model keeps its DCC-authored size,
so conversions must target a *class* (hero/enemy/prop/decor), not a constant.

---

## 3. Converter pipeline audit

### 3.1 FBX pipeline (`fbx_import.cpp` + `fbx_to_pod`, ufbx)

Verified from `src/tools/ufbx/ufbx.c`:

- `ufbx_load_opts.target_axes = ufbx_axes_right_handed_y_up` — glTF/POD-compatible axis space ✓.
- `target_unit_meters = 1.0f` → `ufbxi_scale_units()` (`ufbx.c:24983`) computes
  `unit_scale = scene.settings.unit_meters / 1.0` and scales the **root node**, which our
  `geometry_to_world` baking folds into every vertex.
  - **FBX authored in centimetres (0.01 m/unit) → all positions divided by 100.** Blender/Marmoset
    FBX (metres, Blender-unit=1m) keep 1:1. 3ds Max/Maya default FBX is cm → everything looks
    100× too small until you discover the scale option. This asymmetry is invisible in the UI.
- All node transforms are **baked into vertices** by `fbx_parse`; every emitted `PODNode` gets an
  identity matrix. **No POD hierarchy, no skeleton, no animation** (FBX rigs/skins and clips are
  dropped; `model.num_frames=0`). `fbx_to_pod` then multiplies `opts.scale` into baked positions.
- **UV bug (confirmed from code):** for left-handed FBX sources, ufbx mirrors the scene and flips
  V (`model.uv_v_flipped = true`, `fbx_import.cpp:175`). `fbx_to_pod` flips V **unconditionally**
  when `opts.flip_v` (`pod_convert.cpp:410-415`) → **double flip → textures vertically mirrored**
  for 3ds Max / Unity-style FBX. `glb_to_pod`'s unconditional flip is correct for glTF; the FBX
  path must skip the flip when `model.uv_v_flipped` is set.
- Texture resolution (`material_diffuse_file`) handles FBX→PBR base_color; subfolder search
  (`images/`, `textures/`, `maps/`) is supported — documented behaviour ✓.

### 3.2 glTF pipeline (`gltf_import.cpp`, tiny_gltf v3; `gltf_export.cpp`)

- Correctly keeps the hierarchy: PODNode TRS/matrix, skin joints (renamed with `Bone_`/`Control_`
  prefixes — matches the game's `strncmp(name,"Bone",4)` skeleton scan), animation clips extracted
  per-node with CTs/STEP/LINEAR resampling.
- **Scale application is inconsistent and buggy at the root node** (`gltf_import.cpp`):
  - `m.positions *= opts.scale` (line 243-245), **and**
  - root node with matrix: `matrix[col][0..2] *= scale`, `translation *= scale` (351-363), **and**
  - root node with TRS: `translation *= scale`, **`scale[xyz] *= scale`** (381-388).
  → A root node with authored scale (e.g., 0.01 unit-convert, or an assembly root) gets **squared**
  scaling. Wire root scale must be treated as unit-invariant: multiply *world positions* (mesh
  positions *and* root translation) by `opts.scale`; leave the root **scale channel** untouched.
- Clips: `translation` channel of the root is scaled, `scale` channel is not — correct once the root-
  scale double-apply above is fixed.
- `find_uv_accessor` honours `TEXCOORD_N` from `base_color_texture.tex_coord` ✓; ignores
  `KHR_texture_transform` (offset/rotation/scale) — edge case for DCC packs that rely on it.
- Materials map only base-color → POD diffuse; PBR metal/rough/normal are discarded by design
  (the game has no PBR), but **alpha matters**: `alphaMode=BLEND/MASK` reaches the POD only via
  `pm.opacity` — textures carrying alpha get **ETC1-RGB** below (§3.4) → alpha is lost.
- `gltf_export.cpp` writes mesh UVs **as-is** (bottom-origin POD UVs go into GLB unflipped).
  Round-trip POD→GLB→POD is self-consistent (import flips once), but the exported GLB displays
  **upside-down textures in Blender/other DCCs** unless the user flips. Recommend flipping V on
### 3.3 POD writer (verified against real files + PowerVR spec PDF)

- Chunk grammar matches the official "POD File Format Specification" (PowerVR Tools 2021 R2):
  `1000 Version`, `1001 Scene`, header stats, `2012 Mesh/2013 Node/2014 Texture/2015 Material`
  containers, `6xxx` mesh blocks, `5xxx` node blocks, `9xxx` vertex-block types.
- Omitted chunks `1002` (Export Options) and `1003` (History) are **informational only** per spec
  (p.11) — safe to skip ✓. `3026` (Material Flags), `3009-3017` (secondary texture indices) are
  optional; the game's material system reads `3000/3001/3002/3004` which we write ✓.
- Node encoding: static nodes written as 5004/5005/5006 or matrix 5010; the game's
  `GetWorldMatrixNoCache` reads both ✓. Stock game PODs additionally ship one-frame 5007/8/9 anim
  arrays + 5012 flags even for static meshes; not required for correctness (ruby + game both handle
  static TRS), but worth matching for byte-perfect parity with the SDK renderer.
- Mesh encoding: our writer emits separate 6006/6007/6010 vertex blocks; the game reader supports
  both separate and interleaved 6014. Round-trip verified bit-exact on `knight.POD`.
- U16/U32 indices: our writer uses U32 block type (component type 17); stock files store U16
  where possible. Game SDK converts; verified by load. (Size optimisation: emit U16 when
  `max_index < 65536`.)

### 3.4 Texture pipeline (PVR / ETC1 / TEX)

- Stock header scan (`bat_blue_2x.pvr`): `{52 | 512 | 512 | mip=0 | flags=0x10036 | data=131072 |
  bpp=4 | mask FFFFFFFF | "PVR!" | 1 surface}` — i.e. the game's legacy 52-byte header with
  low-byte 0x36 = ETC1 and **upper flag bit 0x10000 set** (twiddle/mipmap bit), `mip_count=0`.
- Our encoder emits `{52 | h | w | mips=1 | flags=0x36 | ... }` — same base header, missing the
  0x10000 bit and with mip=1 instead of 0. The game's `PVRTTextureLoadFromPointer` keys on
  `header_size==52` and low-byte format; batch_converter uses the same 0x36, so this is *probably*
  fine — **flag it as the one remaining byte-level variance to confirm on-device**.
- **ETC1 is RGB-only** → alpha-carrying textures get black/garbage transparency (halos/flicker) on
  characters/particles. `batch_converter.cpp` already supports RGBA8888 (0x12), RGBA4444, RGB565 —
  the POD converter should auto-select **ETC1 for opaque, RGBA4444 (or 0x12 RGBA8888) for alpha**
  when `alphaMode`/PNG has alpha.
- `texture_filenames` is rewritten to the bare `<stem>.pvr` written beside the POD — matches the
  game's `assets/Models/` layout ✓. Gzipped native TEX `.tex.png` path is separate and fine for
  backgrounds.

### 3.5 `.scene` (RLN protobuf) — object scale end-to-end

Wire format (`scene_loader.cpp`, confirmed against `Proto::*` field descriptors in the binary):
`Tag 4 Position{1:X,2:Y} | Tag 5 Depth(Z) | Tag 6 Rotation | Tag 7 Scaling(uniform) |
Tag 8 LocalAabb | Tag 9 Hidden | Tag 10 OnLoad`, plus `ObjectLibrary { 2: ObjectTemplate {
1: SceneObject, 2: scaling } }` embedded in scenes.

- Ruby writes `Tag 7 = obj.scale_x`; the game reads the same value into `SceneObject@+156`
  (§1.3). **The scale path is a direct 1:1 wire ↔ world contract — correct.**
- `template_scaling` (from ObjectLibrary `scaling`) is **multiplied into scale at render time in
  ruby** (`object_world_matrix`). If the upstream editor *also* baked template scale into `scale_x`,
  ruby double-scales. Needs a data check on a real template object (see §7 todo) before trusting
  the multiply.

---

## 4. Ruby visualiser vs in-game parity

| Concern | Ruby (src/tools) | Game (decompiled) | Verdict |
|---|---|---|---|
| Scene-object world matrix | `M = T·Rz(rot_y·deg)·S(scale·template)` (`scene_workspace.cpp:158`) | `M = T·Rz(angle@144)·S(@156)` | **Match** (minus template factor) |
| Model-node matrix | `get_node_matrix` (TRS+matrix+parents) | `GetWorldMatrixNoCache` (same) | **Match** |
| Feet offset / Y snap | `pod_feet_offset` + `fo*s` | char controllers use colliders | n/a (editing only) |
| Preview camera | auto-fits `distance = radius*2.5` | authored camera/fixed zoom | **Differs by design** → size *perception* mismatch |
| Model preview "Scale" slider (`model_scale`, 0.1–10) | display-only transform | no equivalent | **Confusion source** — affects only preview |
| Scene scale editor | min 0.001 | render-cull at `< 0.01` (DrawModels +160) | **Parity gap**: sub-0.01 objects render in ruby but vanish in-game |
| LocalAABB on scale edit | **not recomputed** on `scale_x` change (`asset_viewer.cpp:4227`); verbatim on write | ground-mesh/sprites read persisted rect | **Gap**: stale AABB → culled/glitchy in-game after manual scale |
| Convert modal presets | hero `20u`, statue `62u` (`asset_viewer.cpp:13346`) | ash=74.2, dragonkin_statue=100.6 | **Wrong data** — the #1 UX bug |
  export (with option) to make GLB the DCC-truthful interchange format.
---

## 5. Edge-case ledger

| # | Case | Location | Behaviour | Severity |
|---|---|---|---|---|
| E1 | cm-authored FBX | `fbx_import.cpp:157` | positions ÷100 at load; user never told | **High** |
| E2 | root-node-scaled glTF + `--scale` | `gltf_import.cpp:243,351,381` | scale applied twice | **High** |
| E3 | left-handed FBX UVs | `pod_convert.cpp:410` | double V flip | **High** |
| E4 | convert presets wrong refs | `asset_viewer.cpp:13346` | hero→20u instead of ~70u | **High (UX)** |
| E5 | alpha textures → ETC1-RGB | `pod_convert.cpp:encode_texture` | transparency lost | Medium |
| E6 | scene `scale < 0.01` | ruby allows 0.001; game culls | invisible in-game | Medium |
| E7 | LocalAABB stale after scale edit | `asset_viewer.cpp:4227ff`, `scene_loader` write | visible mismatch/glitch | Medium |
| E8 | `template_scaling` double-multiply risk | `scene_workspace.cpp:163` | unsure until data check | Medium |
| E9 | huge scene scale | any >~10³ | float AABB overflow / RESCALE_NORMAL lighting breaks / far-off frustum | Medium |
| E10 | stock knight-style 1-frame anim arrays | `pod_writer.cpp` static-TRS nodes | game reads static TRS fine (verified) — recompute `num_frames` | Low |
| E11 | FBX skinned/animated | `fbx_import` | rigs & clips dropped silently | Low-Med (feature gap) |
| E12 | PVR flags `0x36` vs `0x10036`, mip 1 vs 0 | `pod_convert.cpp` | on-device verification needed | Low |
| E13 | 1002/1003 / 3009-3017 omitted | `pod_writer.cpp` | informational only per spec — OK | None |
| E14 | U32 indices always | `pod_writer.cpp` | works; U16 smaller | Low |
| E15 | `KHR_texture_transform` in glTF | `gltf_import.cpp` | ignored | Low-Med |
| E16 | GLB export UVs not flipped | `gltf_export.cpp:298` | GLB looks flipped in Blender | Medium (interop) |
| E17 | obj pipeline `uv_v_flipped=false` always | `obj_loader.cpp:301` | OBJ always top-origin — viewer handles; converter ignores OBJ | Low (no OBJ→POD yet) |
| E18 | fbx_import.h doc claims "normalized to unit cube" | `fbx_import.h:13` | stale doc — code keeps real units (correct) | Low (doc) |

---

## 6. Empirical validation (what I ran)

| Test | Command | Result |
|---|---|---|
| POD writer round-trip | `/tmp/pod_dump knight.POD /tmp/knight_rewrite.POD` + `/tmp/pod_diff` | **pos/nrm/uv/idx maxdiff = 0.0** |
| GLB→POD size fidelity | `bin/ruby --glb2pod MEGA/statue.glb --scale 1.0` | bbox **identical** to shipped `statue.POD` (927.272×975.663×6514.222) |
| Crown GLB default | `--glb2pod King_Crown.glb` | 221u tall @x≈2520,y≈-1730 — authored off-origin at DCC scale; confirms default=raw |
| Stock scale corpus | `/tmp/pod_stats` over 20 models | see §2 table |
| PVR header bytes | struct-unpack of `bat_blue_2x.pvr` | `{52,512,512,0,0x10036,131072,4,FFFFFFFF,...,1}` |
---

## 7. Recommended fix plan (stage 2, in priority order)

1. **Corpus-driven scale assist.** Replace E4 refs with measured values: hero/npc **70u**,
   statue/door **100u**, prop **30–130u**, decor/sign **80–460u**; add "target height" auto-fit
   (`scale = target_units / max(bbox_height_Y)`); surface **unit detection** (FBX `unit_meters`
   via ufbx metadata, glTF meters assumed) and show "resulting size in game units" live.
2. **E1:** Make FBX conversion explicit: keep `target_unit_meters`, but report the source unit and
   the effective positional scale; offer `--unit cm|m|in|auto` so cm-FBX models are not 100× wrong.
3. **E2:** Fix `gltf_import.cpp` root scale application (scale positions+translation only; leave
   node scale channels untouched). Add regression test: glTF with root scale 10 + `--scale 2`
   must produce world-height = 20× *baked* height, never 40×.
4. **E3:** In `fbx_to_pod`, flip V only when `!model.uv_v_flipped` (mirror the viewer's `dcc_uv`
   logic at `asset_viewer.cpp:2334`).
5. **E5:** `encode_texture` — ETC1 for opaque, RGBA8888(0x12)/RGBA4444 for alpha; honor
   `alphaMode` from the importers.
6. **E6/E7:** clamp scene-object scale UI to ≥ 0.01 in ruby, recompute LocalAABB on any
   transform/scale edit (only rewrite `Tag 8` when geometry-holder changed; otherwise preserve).
7. **E8:** data-check a stock template object: is template `scaling` already baked into `Tag 7`?
   Adjust `object_world_matrix` accordingly (single-source `effective_scale`).
8. **E16:** add `flip_v` option to `gltf_export_glb` (default on) so GLB interop is DCC-truthful.
9. **E14/E10:** emit U16 indices when possible; match stock 1-frame anim-array encoding behind an
   option for SDK-pixel-parity.
10. **E12:** byte-level PVR header parity test vs a stock `.pvr` and an on-device/game-binary
    smoke test (check `mip_count=0`, flags=0x10036, masks=0xFFFFFFFF).
11. **Tests to add:** GLB→POD→GLB identity; FBX unit-matrix (cm/m/in); UV double-flip regression
    (left-handed FBX); scene-object scale<0.01 cull parity; LocalAABB refresh on scale edit;
    numeric converter tests wired into `tests/`.

### Where each file lives (absolute)
- `/home/quantumcreeper/SwordigoDesktop/src/tools/fbx_import.cpp` (E1, unit report)
- `/home/quantumcreeper/SwordigoDesktop/src/tools/gltf_import.cpp` (E2 root scale; E15)
- `/home/quantumcreeper/SwordigoDesktop/src/tools/pod_convert.cpp` (E3 FBX flip, E5 texture format, E12 PVR hdr)
- `/home/quantumcreeper/SwordigoDesktop/src/tools/asset_viewer.cpp` (E4 presets, E6/E7 UI+LocalAABB)
- `/home/quantumcreeper/SwordigoDesktop/src/tools/scene_workspace.cpp` (E8 effective scale)
- `/home/quantumcreeper/SwordigoDesktop/src/tools/gltf_export.cpp` (E16)
- `/home/quantumcreeper/SwordigoDesktop/src/tools/pod_writer.cpp` (E10/E14)
- `/home/quantumcreeper/SwordigoDesktop/src/tools/pod_loader.cpp` (E17 docs, validator)

---

## 8. Animation export (POD → GLB): four real defects, and a two-sided proof

### 8.1 Symptom
Mid-clip the torso sheared into stacked layers that were absent on the bind pose;
the rig then rotated as one rigid body instead of posing per bone; and the model
translated off-origin while still spinning. Animation-only — the same mesh posed
statically was clean. That reads as three bugs and was really one, plus three
more behind it.

### 8.2 Root causes (all in `gltf_export.cpp` unless noted)

1. **`inverseBindMatrices` were derived from animation frame 0, not the bind pose.**
   The exporter built IBM from `get_node_matrix(i, 0.0f)` unconditionally. But
   `av::skin_mesh()` — the engine's own authority — uses `node.bind_matrix`
   whenever `has_bind_matrix` is set, i.e. the pose captured from the base model
   *before* an animation-only POD merge overwrote the anim streams
   (`pod_loader.cpp`, the merge site). For a merged clip those are two different
   poses, so every joint's skin palette came out post-multiplied by that joint's
   bind → frame-0 delta. **This is the shear.**
   *Measured:* 188,599 units of vertex displacement on a 52.8-unit model.
   *Control:* meshes whose frame 0 already equals bind showed **exactly 0.0000**,
   so the comparison was exact and not noise.
2. **POD quaternions were exported without the documented xyz negation.**
   `pod_loader.cpp:1198` records that `libswordigo_arm32.c::$c` conjugates POD
   quaternions (x,y,z → −x,−y,−z, w kept), and `local_mat4_from_quat` applies it.
   `gltf_import.cpp` honours it in both directions (2 sites + animation).
   `gltf_export.cpp` wrote POD quaternions **raw**, so every exported rotation was
   the *inverse* of what the game applies. **This is the rigid-body spin.**
3. **Channel liveness was resolved against the static TRS field, not `stream[0]`.**
   The engine's precedence puts `stream[0]` first; the exporter consulted the
   static field first. Aligned on both the rotation and translation paths.
4. **Inverse pivot threshold disagreed with the engine (exporter `1e-9`, engine `1e-8`).**
   Between those two values the exporter emitted an exploded inverse where the
   engine substitutes identity — a genuine band where a limb flies off.

### 8.3 Verification — two directions, against two *different* authorities

The point of using different authorities is that a shared bug can't hide behind
itself.

| Direction | Authority | Test | Result |
|---|---|---|---|
| **export** POD → GLB | the engine's own `av::skin_mesh()` | `tests/glb_animation_skinning_test.cpp` | skeleton **0.0000** and playback **0.0000–0.0001 units** (0.00 % of the 52.82-unit model radius) on `hiro_die` (mesh 4 frame 20), `hiro_run` (mesh 2 frame 16) and `hiro_stand` (mesh 4 frame 3). **24/24 checks.** |
| **import** GLB → POD | an evaluator written directly from the glTF 2.0 spec | `tests/glb_import_spec_conformance_test.cpp` | **186,576** pairwise joint-distance samples over **6** clips: at-frame error **0.000001 units**. 12/12 checks. |

The import-side invariant is deliberately convention-free: pairwise inter-joint
distance is unchanged by any change of basis, by the quaternion conjugation the
importer applies, and by handedness flips — so POD and glTF may disagree about
axes and the two implementations still compare exactly. That single invariant
covers every way this layer can be wrong: shear leaked into a rotation basis
(changes lengths), local-vs-world space mix-up (distances drift), accumulated
root translation (distances run away), quaternion sign / long-way slerp (sampled
*between* keys, so it shows), and joint-index mismatch (wrong bones entirely).

### 8.4 The one non-zero number, and why it is not a bug

`pilot.glb`'s `death` clip is the only one in the fixture whose source keys sit
**off** our resample grid (keys at `0.0133 + n·0.0333`, ours at `0.0000 + n·0.0333`).
Between two stored POD frames the source curve has a kink that a single lerp
cannot follow, so sub-frame error is bounded at **1.759 % of joint spread** —
inherent to POD's dense-grid representation, not a conversion error. At frames
themselves that same clip agrees to **0.000001 units**. The test therefore asserts
"at frame" and "between frames" separately, against an absolute tolerance and a
bounded one respectively; collapsing them into one number would either hide a
real bug or fail on a format limitation.

### 8.5 Fidelity note (not a defect, but it changes round trips)

`av::gltf_import_glb()` renames every joint whose name does not already begin with
`Bone`/`Control` (or equal `CenterPoint`) to `Bone_<name>`. A GLB → POD → GLB
round trip therefore does **not** preserve bone names, and any name-based
correspondence against the source rig has to go through that rewrite.

### 8.6 How to reproduce

```bash
cmake -S . -B build-gg -DSWORDIGO_DEV_MODE=ON
cmake --build build-gg -j"$(nproc)"

# export direction: exported GLB must reproduce the engine's own skinning
./bin/tests/glb_animation_skinning_test

# import direction: our importer must match a spec-derived evaluator
./bin/tests/glb_import_spec_conformance_test
SWORDIGO_GLB_FIXTURE_DIR=/path/to/third-party/glbs \
  ./bin/tests/glb_import_spec_conformance_test    # default: $HOME/smario/smashroyale.io
```

Both skip loudly, exit 0, when the assets they need are absent.

---

## 8b. Animation import (GLB → POD): the vertices and the skeleton disagreed about their unit

### 8b.1 Symptom

A converted clip looked **correct at the bind pose** and tore itself into long
stretched sheets the moment it animated. On `pilot.glb` the skinned mesh measured
**62–130× its own baked size** on every frame; the static base model (which the
viewport draws verbatim, unskinned, at frame 0) looked fine. The failure is
invisible at rest by construction: every skin matrix is the identity there, so a
unit mismatch has nothing to act on.

### 8b.2 Root cause

`pod_loader::skin_mesh()` evaluates a skinned vertex as

```
v_local = inverse(meshWorld) · world(j,f) · inverse(bind[j]) · bindWorld[mesh] · p_pod
```

while glTF defines the same vertex as `world(j,f) · IBM[j] · p_gltf` — a skinned
mesh deliberately ignores its own node transform, and `IBM[j]` is the file's
accessor or the **identity** the spec falls back to when a file ships none.
`gltf_import.cpp` was baking `p_pod = p_gltf`, which makes the two agree only when
`bindWorld[mesh]` is identity. `pilot.glb` breaks that: its mesh node hangs off an
`Armature` scaled ×0.01 while its vertices are authored **bone-local with no
inverseBindMatrices at all**. The result was a ×100 factor between the vertex
space and the joint space — harmless at bind, fatal under animation, because the
joint translations (measured in ROOT units) were being added to vertices measured
in MESH units.

Equating the two expressions gives the rebase the importer owes the runtime:

```
p_pod = inverse(bindWorld[mesh]) · bind[j] · IBM[j] · p_gltf
```

— move every vertex out of its glTF skin space and into the mesh node's LOCAL
space, which is the space native PowerVR PODs store vertices in.

Two details that are easy to get wrong:

1. **One skin can be shared by many mesh nodes**, each with its own pivot
   (`statue.glb`: 10 part meshes on one 512-joint skin), and the runtime uses the
   pivot of the node each vertex belongs to. Rebase per mesh node, not once per skin.
2. **`bind[j]` must be the joint's rest world, never `inverse(IBM[j])`.** Bind
   matrices are not persisted in a POD file, so `pod_loader`'s animation merge
   rebuilds every joint's bind from `get_node_matrix(base, j, 0)`. Deriving the
   rebase from the importer's own `use_ibm` heuristic instead makes it cancel to
   the identity whenever a file carries no real IBM — which is how `statue.glb`
   ended up baking its ×1258 wrapper straight into the vertices.

### 8b.3 Verification

`tests/glb_import_skinning_coherence_test.cpp` converts real GLBs through the
shipping pipeline (`gltf_import` → `pod_write` → `pod_load`), then asserts the
invariant that has no way to be accidentally satisfied:

```
diag(bbox(skin_mesh(model, f))) ≈ diag(bbox(mesh.positions))   for all frames
```

`skin_mesh()` returns vertices in the same space as `mesh.positions`, so a rig
that merely poses keeps a comparable bounding box. Measured on the 54-GLB
`smashroyale.io` corpus (39 animated clips), with the fix disabled and re-enabled
via a temporary compile-time toggle:

| | clips incoherent (ratio > 1.5) | worst ratio |
|---|---|---|
| before | **39 / 39** | 130.4 |
| after | **0 / 39** | 1.29 |

`pilot.glb` per clip afterwards: idle 1.18, walk 1.03–1.12, run 1.04–1.11,
hit 1.03–1.09, slash 0.95–1.05, death 1.01–1.29. Script:
`.scratch/glb_corpus_sweep.sh`; per-asset metric: `.scratch/pod_size_ratio.cpp`.

### 8b.4 A correct importer is not a correct file on disk

Every check above is a **self-consistency** check: it converts a GLB and compares
the resulting POD to itself. That means a `.POD` baked by an *older* converter
passes all of them and still renders wrong — which is exactly what happened to
`soldier-v1`, the model whose shoulder kept tearing after §8b was fixed.

The on-disk `soldier.POD` measured: baked mesh **0.88 m mean error** against every
frame of its companion `walk` clip (worst 1.22 m, and **all 59,674 vertices** off
by more than 5 cm), while its bind pose skinned to itself to 0.0000. Re-converting
the same GLB with the same binary produced 0.008 m. Two diagnostics pin the
signature:

| | stale `soldier.POD` | fresh conversion | glTF spec |
|---|---|---|---|
| bind bbox diag | **1.1817** | 2.1035 | **2.1035** |
| bind bbox centre Y | **0.500** | 0.890 | **0.890** |
| mean \|Δ\| vs walk, 6 frames | **0.898 m** | 0.008 m | — |
| max animated triangle edge | **47.3** (23.3% of diag) | 26.5 (13.0%) | — |

The delta is dominated by a **uniform vertical offset** (+0.88 m, σ≈0.19), i.e. the
pre-rebase bake: the mesh lived in a different space from the skeleton. Nothing
about the file is malformed — it is a perfectly valid POD, just not *this* model.

So §8b's fix must be paired with a re-conversion, and the shipped CLI is the
only thing that decides which is which. `pilot.POD` had the same problem (bind
bbox 6.6965 vs 2.0411 fresh) from the same batch. Both were regenerated in place;
backups are at `/tmp/soldier_backup/` and `/tmp/pilot_backup2/`.

Consequences worth keeping:

- **Re-convert after every importer change.** There is no version stamp in the
  POD format, so a stale file is indistinguishable from a fresh one by
  inspection. `cmp` against a fresh conversion is the cheap test.
- `pilot.glb` converts with `0 textures`: its albedo is declared through
  `EXT_texture_webp`, which the PVR/ETC1 exporter does not read. Separate,
  pre-existing, and untouched here.

### 8b.5 The spec bind box is now an outside authority in the test

`tests/glb_import_skinning_coherence_test.cpp` gained one check that does not
compare the POD to itself: it evaluates the glTF by the spec
(`v = Σ w_k · (world_rest(joint_k) · IBM_k) · p`, rest worlds composed from each
node's static local TRS, `IBM` = identity when absent) and requires the POD's
baked mesh pushed through `get_node_matrix(mesh_node, 0)` to land in the same
place — same diagonal, same centre.

| asset | spec diag | POD diag | ratio | centre offset |
|---|---|---|---|---|
| `soldier.glb` | 2.1035 | 2.1035 | 1.0000 | 0.0000 |
| `minecraft_bee.glb` | 987.7672 | 987.7672 | 1.0000 | 0.0000 |
| `statue.glb` | 5.2843 | 5.2843 | 1.0000 | 0.0000 |
| `pilot.glb` | — | — | — | **skipped** (quantised POSITION) |

With the rebase pass compiled out the same check reports `soldier` at ratio
**0.0100** and a **41.9 %** centre offset — a 100× miss, so the bound is nowhere
near the signal. `pilot.glb` is skipped with a printed reason rather than
reported: it requires `KHR_mesh_quantization` and stores POSITION as SNORM short,
so evaluating it would measure the test's reader rather than the importer.

### 8b.6 Not fixed here

`statue.glb` is a strict no-op under the rebase change and keeps a **separate,
pre-existing** defect: with no `inverseBindMatrices` and a 10-mesh-node skin, its
bind display measures 0.91 where a spec evaluator says 10.10, and its clips
produce 1132–1655 where the spec says 8.5–10.5. Independent of the rebase. Note
that the §8b.5 world-box check *does* pass for it — the divergence only appears
once per-clip animation is sampled, which is where to look next.

### 8b.7 How to reproduce

```bash
cmake --build build-gg -j"$(nproc)"
./bin/tests/glb_import_skinning_coherence_test              # pilot + soldier + bee + statue
SWORDIGO_GLB_ASSETS=~/smario/smashroyale.io/assets \
  ./bin/tests/glb_import_skinning_coherence_test            # whole corpus
./.scratch/glb_corpus_sweep.sh ./bin/ruby /tmp/out /tmp/report.txt   # before/after

# is an on-disk POD stale?  (dumps skinned world verts, compares to the spec)
g++ -std=c++17 -O2 -I src/tools -I src .scratch/pod_dump_world.cpp \
    src/tools/pod_loader.cpp -o /tmp/pod_dump_world
/tmp/pod_dump_world <poddir> soldier 0 > /tmp/ours.txt
python3 .scratch/spec_bind_bbox.py <model.glb> /tmp/ours.txt

# and the animated companion-clip direction
python3 .scratch/soldier_spec_eval.py /tmp/ours.txt <model.glb> \
    <motions.json> walk 24
```

---

## 8c. Four latent defects found while verifying 8b (all fixed)

None of these were causing the shoulder tear — 8b.4 was. They were found by
reading the im/export paths closely during the 8b investigation, are all
*conditional* (they need a particular input to fire), and each would have been
blamed on something else when it did.

### 8c.1 The rigid bake chose bones while the model was standing still

Pinned by `tests/rigid_skin_refine_test.cpp` (33 checks over soldier / pilot /
statue, 0 failures): some vertices must change bone, deviation from smooth
skinning may only improve, positions/indices/uvs/normals must be identical to
the max-weight bake, and an empty clip list must reproduce that bake exactly.

The engine's `C_Matrix4Vector3ArraySkin` reads **one** bone index per vertex and
ignores weights (more_model_research §6), so a smooth-skinned rig has to be
collapsed to one bone per vertex before it is written. The bake chose that bone
with `argmax(weight)` **at the bind pose** — a decision made from a single
instant, for a mesh that only exists to be animated.

`refine_rigid_skin()` (src/tools/gltf_import.cpp) now scores each vertex's own
top-4 influences against every pose of every clip in the model and keeps the one
that tracks it best. Same 1-bone-per-vertex POD, same file format, chosen from
how the vertex actually moves.

| model | vertices | poses | re-bound | worst deviation | mean |
|---|---|---|---|---|---|
| `soldier.glb` (11 clips) | 59674 | 88 | 7306 (12.2 %) | 15.45 → **11.04** | 1.873 → 1.762 |
| `pilot.glb` (6 clips) | 14495 | 48 | 1369 (9.4 %) | 30.46 → **25.46** | 2.146 → 2.038 |

(deviation = per-vertex distance from the full smooth-skin result, model units;
worst = max over vertices and poses, mean = mean over vertices at bind.)

A different bone wins on 9–12 % of vertices and the worst-case deformation drops
28–44 %. With no clips available the function falls back to max-weight, so
callers that convert an unanimated model see byte-identical output.

Because the collapse now depends on the clips, the base model and its clip PODs
must be regenerated **together** — a stale base with fresh clips is exactly the
class of mismatch the sidecar in 8c.2 exists to catch.

### 8c.2 A POD carries no version, so a stale bake is invisible

Every check in §8b.5 compares a POD against itself or against the glTF, and a
stale file passes both — `soldier.POD` was a *valid* POD of a model whose mesh and
skeleton disagreed about their space, and it matched itself to 0.0000. The POD
format's only version slot is the PowerVR `AB.POD.2.0` string, which the game
engine parses.

So provenance lives in a sidecar instead: `<file>.POD.meta`, a flat JSON object
written next to every converted POD.

```json
{"tool":"swordigo-pod-converter","converter":"glb2pod","revision":"4",
 "source":"/…/soldier.glb","rigidSkin":true,"meshVertices":59674}
```

`pod_load()` calls `pod_warn_if_stale()` once per path (`pod_stamp.cpp`), which
names both revisions and says to re-convert. Native Swordigo assets have no
sidecar and are never mentioned. Bump `SWORDIGO_POD_PIPELINE_REVISION`
(`pod_pipeline_revision.h`) whenever the importer changes what it writes.

### 8c.3 Two animation heuristics in the companion path

Both in `gltf_import.cpp`, in the `motions.json` importer:

- **Unit auto-detection measured one keyframe.** When a translation track's
  *first* sample was more than 20× the node's rest offset, the *entire* channel
  was multiplied by `base_len / track_len`. A bone that merely starts extended
  (common — an outstretched limb at frame 0) had its whole motion rescaled by an
  arbitrary constant. Now the ratio is measured over the whole track's RMS and
  only accepted when it is a real unit pair (10, 100, 1000, 39.3701, 3.28084,
  either direction) — never a plain rescale.
- **`is_root` included grandchildren of root.** A translation channel took the
  global `--scale` when its node was a root **or its parent was one**. At
  `--scale 1` (m/source units) the two branches agree and nothing shows; at the
  GUI's 80× Sketchfab / 100× cm→m presets exactly one generation of bones was
  scaled while its siblings were not, and the rig jumped by the whole factor.
  Now only genuine roots (`rigBinding` parent == null, or node parent == -1) carry
  the global scale.

### 8c.4 The clip browser and the converter disagreed about fps

`gltf_inspect_animations()` (the list the model-convert dialog shows) computed
duration as the max last-key over **all** samplers and fps as a flat 24,
while `gltf_import_all_clips()` derived the span from the 90th percentile of
multi-key samplers and the rate from key density. The two therefore advertised
different frame counts for the same clip. Both now call one function,
`derive_clip_timing()`.

That change exposed a second problem in the *companion* path, which is where
most real rigs come from. Companion clips are mostly **2-key constant tracks**
(no motion) mixed with a few densely-keyed ones:

| soldier clip | tracks | keys (min/med/max) | plain median dt | derived fps |
|---|---|---|---|---|
| `idle` | 72 | 90 / 90 / 90 | 0.0333 | 30 ✓ |
| `walk` | 72 | **2** / 2 / 32 | 1.0333 | **1 ✗** |
| `reload` | 72 | **2** / 2 / 100 | 3.3 | **0.3 ✗** |
| `death` | 72 | **2** / 2 / 106 | 3.5 | **0.29 ✗** |

A median over all tracks is a median over the *constant* ones (70 of 72 for
`walk`), and yields 1 fps — a 2-frame walk clip. Key density is now measured
from tracks with ≥ 4 keys when any exist, in both the GLB and companion paths.
All 11 soldier clips then derive 30 fps, matching the source spacing exactly.

The engine still plays at a hardcoded 24 (`CreateAnimationFromFile`), so the
default `--anim-fps 24` is unchanged; `--anim-fps 0` now means "authored rate"
consistently in both paths.

### 8c.5 `EXT_texture_webp` textures converted with zero textures

`EXT_texture_webp` is a **replacement** mechanism, not a fallback: a conforming
file may declare it in `extensionsRequired` and name its image *only* inside
`extensions.EXT_texture_webp.source`, leaving the core `source` field absent.
`gltf_import.cpp` read `textures[i].source` alone, got −1, and dropped the
material's albedo — `pilot.glb` converted with "0 textures" and said nothing.

Two independent gaps, both closed:

- **Resolution** — `texture_source_image()` reads the extension when the core
  field is absent. `pilot.glb` now converts with 1 texture (1024×1024 PVR).
- **Decoding** — stb_image and `QImage` both cover PNG/JPEG/TGA/BMP/GIF and
  neither decodes WebP unless the local Qt ships its webp image-format plugin.
  `image_decode.{h,cpp}` adds a libwebp path (optional CMake dep; decode-only,
  so `libwebpdecoder` suffices, since the pipeline re-encodes *to* PVR/ETC1).
  All four decoder call sites in `pod_convert.cpp` and both Qt sites in the
  viewer (`viewport_3d_widget.cpp`, `glb_model.cpp`) route through it, so a
  WebP payload behaves identically everywhere. Without libwebp the build is
  unchanged and those payloads are skipped as before — note that
  WebP(VP8) → RGBA → ETC1 stacks two lossy steps, so it is a compatibility
  path, not a quality one.

### 8c.6 How to reproduce

```bash
cmake --build build-gg -j"$(nproc)"       # line reads "libwebp 1.6.0 — EXT_texture_webp textures will decode"

# 8c.1 refinement stats are printed on every conversion that has clips
./bin/ruby --glb2pod <model.glb> /tmp/out.POD

# 8c.1 regression test: it must change something, must never regress, must not
# touch the geometry, and must be a strict no-op without clips
./bin/tests/rigid_skin_refine_test
SWORDIGO_GLB_ASSETS=~/smario/smashroyale.io/assets ./bin/tests/rigid_skin_refine_test

# 8c.2 staleness: point a sidecar at another revision and load it
printf '{"revision":"1"}' > /tmp/out.POD.meta
./bin/tests/glb_import_skinning_coherence_test   # or any pod_load() caller

# 8c.4 the browser and the converter must now agree
./bin/ruby --glb2pod <model.glb> /tmp/out.POD --anim-fps 0   # derives 30 for a 30 fps source

# 8c.5 texture resolution
./bin/ruby --glb2pod <pilot.glb> /tmp/out.POD   # "1 textures" where it used to say "0"
```

---

## 8d. The files were correct in ruby_gg and broken in the game: tag 5010 is a no-op

*This is a different bug from §8b: that one was a stale file, this one is wrong
bytes. Both presented as "the model looks wrong in-game but fine in the editor".*

### 8d.1 Symptom

A converted model rendered correctly in the ruby_gg POD viewer, animated
correctly there, and appeared **in the game** as a collapsed stick / "spirit
sheet". It was not skinning-specific: the *static* `mh-60l_dap_usa`, a model
with no bones at all that had worked before, broke the same way.

### 8d.2 Root cause — the writer emitted a tag the runtime discards

The runtime reader is `CPVRTModelPOD::ReadFromMemory` at `0x580AB8`. Its node
section (tag `2013`, `case 0x7DD`) dispatches on the node tags, and the relevant
cases map onto `SPODNode` like this:

| tag | reader case | `SPODNode` field | meaning |
|---|---|---|---|
| 5000 | `0x1388` | `+0` | `nIdx` |
| 5001 | `0x1389` | `+8` | name |
| 5002 | `0x138A` | `+16` | `nMaterialIdx` |
| 5003 | `0x138B` | `+20` | `nParentIdx` |
| 5004 | `0x138C` | locals | static position (3 u32) |
| 5005 | `0x138D` | locals | static rotation (4 u32) |
| 5006 | `0x138E` | locals | static scale (3 u32) |
| 5007 | `0x138F` | `+40` | `pfAnimPosition` |
| 5008 | `0x1390` | `+56` | `pfAnimRotation` |
| 5009 | `0x1391` | `+72` | `pfAnimScale` |
| **5010** | **`0x1392`** | **—** | **`goto LABEL_202` — payload skipped, nothing stored** |
| 5011 | `0x1393` | `+88` | `pfAnimMatrix` |
| 5012 | `0x1394` | `+24` | `nAnimFlags` |
| 5013–5016 | `0x1395`–`0x1398` | `+32/+48/+64/+80` | the four `pn*Idx` arrays |

**Tag 5010 is dead.** Its handler is literally `case 0x1392u: goto LABEL_202;`,
and `LABEL_202` just skips `len` bytes.

Our writer emitted a node's raw 4×4 under 5010 whenever a glTF node carried a
`matrix` instead of TRS — i.e. **every Sketchfab export**, which is how
`mh-60l_dap_usa.glb` was authored (nodes 0, 1, 3 and 5 all use `matrix`). The
consequence chain in the runtime:

1. `pfAnimPosition` stays `NULL`, so `CPVRTModelPOD::GetTranslation`
   (`0x584C08`) returns **without writing its output** — the whole body is
   guarded by `v3 = *(node + 40); if (v3) { ... }`.
2. `pfAnimRotation` and `pfAnimScale` are `NULL` too, so
   `GetScalingMatrix` yields identity and the rotation block yields identity.
3. `GetWorldMatrixNoCache` (`0x584794`) therefore multiplies in an
   **uninitialised translation**, and the node's world matrix is whatever was on
   the stack — model thrown off-axis, collapsed into sheets.

Written the same matrix under **5011** instead and
`GetWorldMatrixNoCache` takes its first branch (`v6 = *(node + 88)`) and uses
the matrix verbatim. No TRS decomposition, no convention risk.

### 8d.3 The second half: a node with *no* transform tag is equally broken

The reader clears a per-node flag at the top of the node case
(`LOBYTE(v18) = 0;`) and only ORs it in 5004/5005/5006. The node-close handler
(`0x800007DD`) is guarded by `if (v18 & 1)`, and *that* is what synthesises the
1-frame `pfAnimPosition`/`pfAnimRotation`/`pfAnimScale` arrays. So a node with
none of 5004–5006, no 5007–5009 and no 5011 has **no transform at all** — the
same undefined-translation failure as above, for a node that merely meant
"identity". The writer now always emits the static TRS, matching stock
exports, which write 5007/5008/5009 unconditionally for exactly this reason.

### 8d.4 Ground truth

Two independent authorities, neither of which is our own loader:

- **The decompilation above**, which is the reader that actually decides.
- **`PVRGeoPODCLI` 2.28**, shipped on the PowerVR Tools volume, run as
  `PVRGeoPODCLI -i=cube.obj -o=cube_ref.pod`. Its output for a static model:

```
scene: … 2004=1 2005=2 2006=1 2007=0 2008=1 2009=30 2017=30 2016=0
mesh:  6000 6001 6002 6005 6014 6018 6019 6020 6003 6006 6007 6008 6009 6010 6011 6012 6013 6021
node:  5000 5001 5002 5003 5012 5007 5008 5009      ← never 5010
```

and from the shipped game's own assets (`rock1.POD`, `hiro.POD`, and
Imagination's `PVRShaman/Example/POD/OGL/skinning.pod`): node transforms are
always 5007/5008/5009 — 1 frame with `nAnimFlags == 0` when static (12 / 16 / 28
bytes), N frames with `nAnimFlags == 7` when animated (e.g. 204 / 272 / 476 for
17 frames). 5010 appears nowhere in any reference file.

Cross-check on `mh-60l`: the glTF-spec world AABB of the two meshes
(`x[-148.42, 100.22] y[0.0011, 67.09] z[-100.69, 100.78]`) is reproduced exactly
by `av::pod_load` on the regenerated POD — so geometry, baking and the node
hierarchy were never wrong; only the tag was.

### 8d.5 What changed

- `pod_writer.cpp`: node matrices go out as **5011**, never 5010
  (`eNodeMatrixUnsupported` is named to make the next use fail review); the
  static TRS (5004/5005/5006) is now **always** emitted, so no node is left
  without a transform; the mesh section gained the stock tags it was missing
  (`6005 NumStrips = 0`, empty `6008`/`6009`/`6011`, and the 64-byte identity
  `6020 pfUnpackMatrix`), putting our mesh tag stream in the same order and set
  as a PVRGeoPOD export.
- `pod_loader.cpp`: a lone 64-byte 5011 is routed to `has_matrix` (the same
  slot 5010 used to fill), so ruby_gg, `gltf_export`, `render_model` and
  `skin_mesh` keep their existing code path. Legacy files written with 5010 are
  still parsed.
- Pipeline revision `4 → 5` (`pod_pipeline_revision.h`), so every POD written
  before this is flagged stale on load.

### 8d.6 Verification

`tests/pod_game_reader_contract_test.cpp` walks the bytes `pod_write` produced
with a tag scanner that shares no code with `pod_loader` — deliberately, because
sharing the reader is what hid this bug — and asserts, per node and per mesh:

- tag 5010 never appears;
- every node carries 5004/5005/5006, 5007/5008/5009 or a 64-byte 5011;
- a raw matrix survives as a byte-for-byte 64-byte 5011 payload;
- every mesh declares `6005 == 0` and a 64-byte identity `6020`;
- every index stream length equals `numFaces * 3 * indexWidth`.

Before the fix the same test reported `5010 count = 2` and `2 of 2 nodes had
none` **on the user's on-disk `mh-60l_dap_usa.POD`**, which is what made the
diagnosis conclusive; it is now 19 checks, 0 failures.

### 8d.7 Not fixed here (flagged, not buried)

- `nFPS` (2017) and `nNumFrames` (2009) are written as 0 for base models, where
a 2021-era PVRGeoPOD writes 30/30. Stock 2013 assets (`rock1.POD`) also write 0,
so this follows the game's own assets, not the newer tool.
- Scene ambient colour (2001) is `(0.2, 0.2, 0.2)` where the reference and every
stock asset write `(0, 0, 0)`. Cosmetic and unused by the shipped renderer, so
left alone.
- Mesh bone indices (6012) are written with `eType = 2` (INT) where stock uses
  `eType = 10` (UINT). Both are 4 bytes, and the runtime reads them through
  `Caver::MeshData::valueSize`, so they are equivalent — but it is a difference
  worth knowing about.

### 8d.8 How to reproduce

```bash
# ground truth you can regenerate at will
PVRGeoPODCLI -i=cube.obj -o=cube_ref.pod     # on the PowerVR Tools volume

# the contract the runtime enforces
./bin/tests/pod_game_reader_contract_test
```

## 8e. The skinned models rendered as an empty silhouette: a mesh's BONEIDX is not a bone index

### 8e.1 Symptom

A converted character (soldier/HiRo, pilot, …) renders correctly in the ruby_gg
viewer but, in the shipped game, draws as a hollow outline — a handful of long
thin triangles where the body should be, with the animation still visibly
running. A *static* converted model (the `mh-60l` helicopter) was reported as
broken alongside it, but for a different, already-fixed reason (§8d).

The two views disagree because they read the file with different code. ruby_gg
uses `av::pod_load`; the game uses `Caver::PODLoader` plus its own sketch of the
skeleton. Only the latter implements the two-hop bone lookup below, and only the
latter fails hard without the table described here.

Scope note, so the claim is not overstated: the *contract* below is read
directly out of the decompile and is not in doubt, and our writer violated it
for every skinned model until this section. The asset that produced the specific
screenshot on disk at the time (a 15:12-era stamp) already carried 6015/6016 but
dropped 6020 and the two empty attribute blocks, so its exact failure mode was
not isolated frame-by-frame; what is certain is that the table was optional in
the generator, which makes the failure silent and state-dependent. It is now
mandatory, so the class is closed regardless.

### 8e.2 Root cause — a mesh's BONEIDX values are SLOT indices

A vertex's bone is resolved in two hops by `Caver::PODLoader::CreateMesh`
(`0x4E4E74`, `OpenSwordigo/arm64_13/functions/Caver/PODLoader/`):

```c
if ( v6[29] >= 1 )                       // Caver::Mesh bone-id stream is non-empty
{
  v101 = *(int *)(pMesh + 264);           // 6018  nMaxNumBonesPerBatch (CAPACITY)
  v104 = (_DWORD *)operator new[](4 * v101);   // slot -> skeleton bone remap
  v105 = **(unsigned int **)(pMesh + 248);     // 6016[0] — no null check
  for ( i = 0; i < v105; i++ )
  {
    v111 = *(_DWORD *)(*(_QWORD *)(pMesh + 240) + 4 * i);   // 6015[i]
    /* std::map lookup/insert keyed by v111 */
    v104[i] = map[v111];                  //           -> Skeleton bone index
  }
  /* every vertex's BONEIDX is replaced by v104[BONEIDX] */
}
```

So the on-disk tags are:

| tag | field | meaning |
|-----|-------|---------|
| 6015 | `pnBoneBatchBoneIdx` | for each batch, the **POD node index** of each bone (stride = 6018) |
| 6016 | `pnBoneBatchBoneCnt` | one entry per batch: how many of those slots are valid |
| 6017 | `pnBoneBatchBoneMax` | per-batch maximum (stock writes 0) |
| 6018 | `nMaxNumBonesPerBatch` | the table's **capacity**, used to size the allocation |
| 6019 | `nNumBoneBatches` | number of batches (stock `/hiro.POD` writes 1) |

and the mesh's **6012 (BONEIDX) stream is an index into 6015's slot space**, not
into the skeleton. `Caver::PODLoader::CreateSkeleton` (`0x4E42F0`) is what turns
a node index into a skeleton bone index: it marks every node named `Bone*` /
`Control*` **plus every ancestor of such a node**, then numbers them in ascending
node order, storing `node index -> bone index` in the map CreateMesh consults.

Ground truth, stock `resources/soldier/hiro/hiro.POD`, mesh 2:

```
6018 = 100        6019 = 1
6015 = [41, 40, 39, 38, 45, 46, 44, 43, 0, …]   (100 slots)
6016 = [8]        6017 = [0]
BONEIDX = 0..7    <- exactly the 8 valid slots
```

Until §8e.3 the writer emitted BONEIDX but **none of 6015/6016/6017/6018/6019**
whenever the producer forgot to fill `bone_batches` — and the batch tags were
also skipped entirely for a *static* mesh, where stock writes `0/0`. A file in
that state runs `**(unsigned int **)(pMesh + 248)` on a null pointer before the
game can draw anything: the mesh is left half-built, which is the empty
silhouette. Note that `v6[29]` (Caver::Mesh byte 116) is the bone-id stream's
`n`, i.e. the guard fires for **every** skinned mesh, not only the ones we
produce. Stock files always carry the table, so this is entirely a
producer-side obligation.

### 8e.3 What changed (`src/tools/pod_writer.cpp`)

1. **6018/6019 are now written for every mesh**, exactly like stock PVRGeoPOD
   (`0/0` for a static mesh; verified against `resources/rock1.POD` and against a
   fresh `PVRGeoPODCLI` export). Previously they were only written when the
   model already carried a batch table.
2. **The batch table is now mandatory for a skinned mesh.** If
   `bones_per_vertex > 0` and no table was supplied, the writer synthesises one
   from the game's own marking rule (`skeleton_node_order()`): marked nodes in
   ascending index order, so slot *i* maps to skeleton bone *i*. It prints a
   warning so a producer that forgot is visible rather than silent.
3. **The capacity is validated.** `6018` is padded up to
   `max(BONEIDX) + 1` if a vertex references a slot beyond the table, because
   the loader indexes it without a bounds check.

### 8e.4 Verification

`tests/pod_game_reader_contract_test.cpp` gained three checks, applied both to a
synthetic fixture (with a supplied table and with none, so the synthesis path is
exercised rather than assumed) and to every converter-stamped asset on the
machine:

- every skinned mesh carries a complete, non-empty table: `6015/6016/6017/6018/6019`
  present, `counts[0] >= 1`, `6015` at least `counts[0]` entries, `capacity >= counts[0]`;
- every entry of `6015` names a node that exists, and every `BONEIDX` value is
  inside the table;
- every static mesh declares `6018 == 0 && 6019 == 0` (the stock shape).

57 checks, 0 failures, including the `abomination_-_doom…-rigged.POD` and
`mh-60l_dap_usa.POD` on-disk assets. Regenerated in place: `resources/soldier/*`,
`resources/mh-60l_dap_usa.POD`, `assets/cathedral.POD`, and the
`soldier-v1` / `pilot` sets (backups under `/tmp`).

A converted skinned mesh now byte-matches the stock tag order:

```
6000 6001 6002 6005 6018 6019 6015 6016 6017 6020 6003 6006 6007 6008 6009 6010 6011 6012 6013
```

### 8e.5 Still open (flagged, not buried)

- Companion “animation-only” PODs (`<model>_<clip>.POD`) carry `numMeshes = 0`
  and no mesh section, which is how stock Swordigo stores clips as well; they
  are merged onto the base model at load time by our own loader, and the game's
  `CreateAnimationFromFile` path has not yet been compared against ours.
- The loader (`pod_loader.cpp`) accepts a skinned mesh without a batch table by
  falling back to identity. That leniency is why this defect was invisible from
  inside ruby_gg; it should warn instead.

---

## 8f. The block grammar itself: every leaf carries a close pair

### 8f.1 Why this took so long to see

§8d and §8e are both *tag* defects — the right value in the wrong place. This
one is a *grammar* defect, and it is the reason neither of the others was
caught by testing: our loader and our writer shared the same wrong idea of what
a block looks like, so files round-tripped perfectly through `ruby_gg` while the
game read rubbish.

A POD is a stream of blocks. What we wrote:

```
<tag:u32> <len:u32> <payload>                       leaf
<tag:u32> <len:u32> ...children... <tag|0x80000000><0>   container
```

What every stock file contains — vanilla Swordigo assets, PVRShaman's example
PODs, and anything `PVRGeoPODCLI` produces:

```
<tag:u32> <len:u32> <payload> <tag|0x80000000:u32> <0:u32>   EVERY block
```

### 8f.2 The authoritative source

`PVRShamanGUI` ships the reference implementation, and headless IDA extraction
recovered its writer intact (`OpenSwordigo/PVRToolsDecomp/PVRShamanGUI/`):

```c
// CPVRTModelPOD::SavePOD @ 0x753370, and its three helpers
__int64 sub_74BA40(FILE *s, u16 tag, int len)   // BEGIN
{ ptr[0]=tag; fwrite(ptr,4,1,s);  ptr[0]=len; fwrite(ptr,4,1,s); }

__int64 sub_74BAB0(FILE *s, i16 tag)            // END
{ LOWORD(ptr[0])=tag; HIWORD(ptr[0])=0x8000;
  fwrite(ptr,4,1,s);  ptr[0]=0; fwrite(ptr,4,1,s); }

__int64 sub_74BB90(FILE *s, i16 tag, _DWORD *data, int count)
{ if (!data) return 1;               // NULL source  ->  no block at all
  sub_74BA40(s, tag, 4*count);
  if (!count) return sub_74BAB0(s, tag);   // empty array still opens+closes
  for (i) fwrite(&data[i],4,1,s);
  return sub_74BAB0(s, tag); }
```

`sub_74BAB0` runs after *every* `sub_74BA40`, without exception. The end tag is
the same tag with the high half set to `0x8000`, i.e. `tag | 0x80000000`, and the
second dword is always `0`. Full write-up and the surrounding evidence:
`OpenSwordigo/PVRToolsDecomp/POD_WRITER_GRAMMAR.md`.

### 8f.3 The four consequences

The helpers settle four questions at once, all of which our writer answered
incorrectly before this change:

1. **Leaves carry a close pair.** Not just containers.
2. **`len` is the exact payload length** (`4 * count` for arrays).
3. **A stream with `n == 0` has NO `9003 data` tag at all.** `sub_74BB90`
   returns immediately on a NULL source. We used to emit a 4-byte placeholder.
   `rock1.POD`'s `6008 TANGENT` block is literally three tags long:
   `9000 eType`, `9001 n=0`, `9002 nStride=0`.
4. **An empty array still opens and closes** — so the block is *present* with
   no payload, which is the same shape from two different rules.

Plus two ordering facts visible in `SavePOD` and in every stock file: scene
children run materials (`2015`), meshes (`2012`), nodes (`2013`), textures
(`2014`); and tags `1002` (exporter options) and `1003` (provenance string) sit
between the version block and the scene block. Neither is read by libswordigo —
both are unknown tags that the reader skips — but they are part of what a stock
file looks like.

### 8f.4 The material block was also short

The scene-header sweep of `sub_580AB8` (cases `0xBB8`–`0xBD2`, tags 3000–3026)
shows the material struct is 176 bytes and the runtime writes to `+12..+44`,
`+112..+132`, `+136`, `+152`, `+168`. Our writer emitted only the six colour /
texture-index tags. Omitting `3009..3017` is the dangerous part: nine
auxiliary texture slots, which stock sets to the `-1` sentinel ("no map of
this kind") and which we left at the `calloc`'d **0** — a perfectly valid
texture index, so the runtime would bind the file's first texture as every map
the material has a slot for. All values now copied verbatim from `rock1.POD`.

### 8f.5 What changed

- `pod_writer.cpp` — `Sink` grew `field_*` helpers that write a whole leaf
  (`<tag><len><payload><tag|END><0>`); every call site converted. The version
  block, `1002`/`1003`, the scene child order, empty attribute streams and the
  full material tag set now match stock. No tag *values* changed — §8d and §8e
  were already right — only the block framing and the missing fields.
- `pod_pipeline_revision.h` — `5 -> 6`.
- `tests/pod_game_reader_contract_test.cpp` — a strict recursive walker
  (`grammar_scan`) that requires the close pair on every block, checks the
  first 27 bytes are the stock `1000 / AB.POD.2.0` block byte-for-byte, asserts
  `1002`/`1003` exist, that no `n == 0` attribute stream carries a `9003`, that
  every material declares all nine `-1` texture slots and its `3026` tail, and
  that no material's slots fall back to 0.
- `.scratch/pod_canon.py` — a canonicalising dumper used to diff our output
  against `rock1.POD` tag-by-tag; it prints an explicit `MISSING close` line so
  this class of defect can never hide again.

### 8f.6 Verification

The mesh, node and material blocks of a freshly converted
`mh-60l_dap_usa.POD` now diff **tag-for-tag and nesting-for-nesting** against
`resources/rock1.POD` — same tags, same order, same depth; only the numbers
differ, because one is a helicopter and the other a boulder:

```
$ python3 .scratch/pod_canon.py --depth=4 ours.POD | grep -c MISSING
0
```

`pod_game_reader_contract_test`: 126 checks, 0 failures. All 21
converter-stamped PODs under the user's asset root, plus every `.POD` with a
`.glb` sibling, were rebuilt with the corrected writer; the vanilla originals in
`assets/resources/soldier/hiro/` were left alone.

---

## 9. Files consulted (evidence index)

- Decompiles: `OpenSwordigo/arm64_13/functions/Caver/PODLoader/*`, `.../Caver/ModelLibrary/*`,
  `.../Caver/ModelComponent/*`, `.../Caver/SceneObject/*`, `.../Caver/TransformComponent/*`,
  `.../Caver/Scene/00000000004F8E80__DrawModels.c`, `.../CPVRTModelPOD/*`,
  `.../Caver/Proto/ModelComponent/*`, `.../Caver/Proto/ObjectLibrary/*`.
- Tools: `src/tools/pod_loader.{h,cpp}` `pod_writer.{h,cpp}` `pod_convert.{h,cpp}`
  `fbx_import.{h,cpp}` `gltf_import.cpp` `gltf_export.cpp` `gltf_glb.h` `asset_viewer.cpp`
  `scene_loader.cpp` `scene_workspace.cpp` `batch_converter.cpp` `ufbx/ufbx.{h,c}`.
- Reference docs: `PowerVR_Tools/PVRGeoPOD/Documentation/POD File Format.Specification.pdf`,
  `docs/formats_and_schemas/pod_3d_model_format_spec.md`.
- Corpus: `/home/quantumcreeper/resources/*.POD`, `/home/quantumcreeper/MEGA/{statue.glb,
  statue.POD, statue_knight.POD}`, `/home/quantumcreeper/SwordigoRefresh/assets/models/*.glb`,
  `src/tools/King_Crown/King_Crown.glb`.
| Chunk grammar | chunk-walk of `knight.POD` vs `/tmp/chunkdump.py` | `1000/1001/2012/2013/2015/6xxx/5xxx/9000-9003` all present; extra `1002/1003/3009-3017/3026/6014` on stock |