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

## 8. Files consulted (evidence index)

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