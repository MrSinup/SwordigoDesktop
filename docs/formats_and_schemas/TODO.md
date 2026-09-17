# Master TODO — Ruby Studio (asset_viewer) & POD/FBX/glTF pipeline

> **Source of truth:** every item below is grounded in one of the research docs in
> `docs/formats_and_schemas/` (and `docs/ruby_parity/`), cross-checked against the
> **current** code on 2026-09-05. Each item has its doc provenance, the exact code
> location, and the current implementation status with the evidence used to judge it.
> Save-file docs (`savefile_*.md`) are intentionally excluded (out of scope).
>
> **[ ] = open    [x] = done    [~] = partially done / needs a follow-up**

---

## 0. Quick status map (chip)

| Area | Done | Open |
|---|---|---|
| Converter E-series (E1–E18) | E1,E2,E3,E4,E5,E6,E10,E12,E13,E14,E15,E16,E17,E18 | E7?,E8,E9,E11 |
| Viewer glitches (issues.md A1–A4) | A1,A2,A3,A4 | — |
| Ruby-parity P0–P3 | camera modifier, gizmo, picking, portal | shadow-cam params, particle preview |
| Animation & skinning parity | bone-batch indices, IBM import, 24 fps resample, dominant-bone | — |
| MCP easy wins | — | scene_edit, ground_mesh, texture_edit, lua_run, TCP |

---

## 1. Converter & POD format

### 1.1 Open

- [ ] **S1d — companion clip PODs are not yet compared against the game's path.**
  Report §8e.5. `<model>_<clip>.POD` files carry `numMeshes = 0` and no mesh
  section (stock does the same); our loader merges them onto the base model, but
  the game's own `Caver::PODLoader::CreateAnimationFromFile` (`0x4E59C0`) has not
  been diffed against ours. Needs the same decompile-vs-writer treatment §8d/§8e
  got: does it index nodes by name, and does it expect the base node table to
  come from a separate file?
- [ ] **S1e — `pod_loader.cpp` should warn, not silently fall back.**
  Report §8e.5. A skinned mesh with no 6015/6016 is loaded with an identity
  slot→bone mapping, which is why this class of defect is invisible from inside
  ruby_gg. Emit a one-time warning naming the file, so the next producer that
  forgets the table is caught by the viewer instead of only by the game.
- [ ] **E8 — `template_scaling` double-multiply data check**
  - Doc: `pod_fbx_gltf_interconversion_report.md` §3.5 / edge E8, line ~257-261.
  - Current code: `scene_workspace.cpp` `object_world_matrix` multiplies
    `obj.scale_x * obj.template_scaling` (`scene_workspace.cpp:163` area). The
    inspector *displays* `template scale ×A` but nothing verifies whether upstream
    editors already baked the ObjectLibrary `scaling` into `Tag 7` (which would
    double it).
  - Do: decode a stock template object (`grass_house.scene` → `woodendoor`,
    `pressureplate`…) and compare `scaling` in `ObjectLibrary{2:ObjectTemplate}` vs
    persisted `Tag 7` in the object's SceneObject; emit a `template_scaling * scale`
    only if they are independent. Wire a one-time diagnostic print in
    `scene_loader.cpp` (`scene_list_templates`) or the inspector.

- [ ] **E9 — huge scene-scale float robustness**
  - Doc: edge E9. Objects with `scale > ~10³` overflow AABB floats / break
    `GL_RESCALE_NORMAL` lighting / far-frustum cull.
  - Do: clamp scale UI hard ceiling + warn; guard AABB computation against non-finite;
    verify with a `scale=10000` test object in ruby vs in-game.

- [ ] **E11 — FBX skinned / animated import (feature gap)**
  - Doc: edge E11. `fbx_import.cpp` drops rigs, skins and clips silently
    (`model.num_frames = 0`, all nodes identity).
  - Do: if/when FBX animation matters — port the glTF skin/clip pipeline (ufbx
    already loads armatures/animations into memory; only the POD emission is missing).

- [x] **E15 — `KHR_texture_transform` (offset/rotation/scale) honoured on import**
  - Doc: edge E15 + §3.2. **Done 2026-09-06** — `gltf_import.cpp` reads the
    extension from the base-colour textureInfo via the materialised extension
    tree (`ti->ext`, same mechanism as the spec-gloss fallback) and also from
    `KHR_materials_pbrSpecularGlossiness.diffuseTexture`, then bakes
    `uv' = T·R(θ)·S·uv` into each mesh's UV stream at import (stderr note per
    baked material). Baked in glTF UV space *before* the converter's `--flip-v`
    game adapter, so composition matches ground truth. POD's single UV channel
    means only the base-colour/diffuse transform is baked (the game cannot
    express per-texture UV transforms); other maps' transforms stay ignored by
    design.

- [x] **E17 — OBJ→POD converter shipped (`--obj2pod`)**
  - Doc: edge E17. **Done 2026-09-06** — new `av::obj_to_pod` (`pod_convert.cpp`)
    reuses the viewer's tolerant `obj_load` (v/vt/vn, quads/ngons fan-
    triangulated, `.mtl` `map_Kd`) and runs the same pipeline as FBX/GLB:
    `--flip-v` game-convention flip (OBJ UVs are DCC top-origin, `uv_v_flipped`
    honoured), `unit_scale * scale` multiplier, `find_source_image` +
    `encode_texture` → game `.pvr` / `.tex.png`, `bake_and_center_model`,
    `pod_write`. CLI: `bin/ruby --obj2pod <in.obj> [out.pod] [options]` (routed
    in `asset_viewer.cpp` alongside `--fbx2pod`/`--glb2pod`). Static geometry
    only — OBJ carries no rigs. Verified on a synthetic textured cube: 36
    verts / 12 tris, UNSIGNED_SHORT indices, texture re-encoded + referenced.
    Part (b) of the old item (viewer `dcc_uv` top-origin tracking) was already
    handled by the viewer.

- [x] **E14 follow-up — U16 index truncation guard in `pod_writer::write_index_block`**
  - Doc: edge E14. **Cleared 2026-09-06** — the writer's width-selecting rewrite
    had already shipped (stale TODO): `pod_writer.cpp` `write_index_block` now
    emits the stock UNSIGNED_SHORT (eType 3, 2-byte) layout when every index is
    < 65536, and 4-byte 32-bit indices (eType 2, size 4 per
    `PVRTModelPODDataTypeSize`) otherwise — which the game's reference 9003
    reader dispatches by data-type size (`pod_master/03_mesh_and_vertex_data.md`
    §3). No truncation remains (`idx & 0xFFFF` only runs inside the proven-u16
    branch). Remaining: a CI regression test byte-comparing a u16-index mesh
    against a stock POD (see §1.1 PVR item pattern).

- [ ] **PVR `mip_count`/flag footnote** (from §3.4 — the one remaining byte-level
    variance, though CHANGELOG says flags now `0x10036`):
  - Doc: `pod_fbx_gltf_interconversion_report.md` §3.4. CHANGELOG confirms header is
    byte-identical for shipped `.pvr` (13 fields verified). Keep as a **regression
    test item**: byte-compare every new converted texture vs a stock `.pvr` in CI.

### 1.2 Cleared / already done (do NOT re-do)

- [x] **E1 FBX `--unit`** (`pod_convert.cpp` CLI; help text updated).
- [x] **E2 glTF root-scale double-apply** (`gltf_import.cpp:354-385` — only world
  translation scaled, node scale channel untouched; CHANGELOG regression-tested).
- [x] **E3 FBX UV double-flip** (`pod_convert.cpp:568-570` skips flip when
  `model.uv_v_flipped`; viewer `dcc_uv` mirrors it).
- [x] **E4 convert presets** (asset_viewer presets Hero~70/Prop~100/Decor~250; game
  bounds readout live).
- [x] **E5 alpha textures → RGBA** (ETC1 only when fully opaque; RGBA8888 for alpha).
- [x] **E6 scene-scale clamp ≥ 0.01** (inspector clamp + comment citing `DrawModels`).
- [x] **E10 static-node TRS encoding** — game reads static 5004/5/6 fine (verified).
- [x] **E12 PVR header parity** — CHANGELOG + byte-verified vs stock.
- [x] **E13 1002/1003** — informational per the PowerVR spec, and now **written**.
  The original "omit them" call was defensible (libswordigo skips unknown tags)
  but it left our files structurally distinguishable from stock. Both blocks are
  back as of S1d, copied from `rock1.POD`. See report §8f.
- [x] **E16 GLB export V-flip** (`gltf_export_glb` `flip_v` default on).
- [x] **E18 stale `fbx_import.h` doc** corrected.
- [x] **POD version-block "close-tag bug" claim → cleared.** `more_model_research.md`
  §14 claimed `0x800003E8` after tag 1000 is invalid. Binary comparison of
  `/home/quantumcreeper/resources/knight.POD` vs current writer output shows **both**
  emit tag 1000 → len 11 → string → end tag `0x800003E8` len 0. Stock is parsed by the
  same `pod_loader` path; the spec treats version as open+close. Not a bug.
- [x] **Bone-batch indices populated** — `gltf_import.cpp:410` now fills
  `m.bone_batches.indices` from remapped skin joints (old "empty indices" concern gone).
- [x] **S1d — the block grammar now matches the reference writer byte-for-byte** —
  report §8f. `PVRShamanGUI` carries the reference implementation
  (`CPVRTModelPOD::SavePOD` @ 0x753370) and headless IDA extraction recovered its
  three block helpers, which settle the framing outright: `sub_74BA40(f,tag,len)`
  opens a block (`<tag:u16><0:u16><len:u32>`), `sub_74BAB0(f,tag)` closes it
  (`<tag:u16><0x8000:u16><0:u32>`), and `sub_74BB90` writes **nothing** for a NULL
  source. So every block — leaf and container alike — carries its close pair; a
  stream with `n == 0` has no `9003` payload; scene children run
  materials/meshes/nodes/textures; and materials carry the full 3000–3026 tag set,
  including the nine auxiliary texture slots which stock sets to the `-1` sentinel
  and we were leaving at the `calloc`'d 0 (a *valid* texture index). Evidence:
  `OpenSwordigo/PVRToolsDecomp/POD_WRITER_GRAMMAR.md`;
  regression: `tests/pod_game_reader_contract_test.cpp` (126 checks) plus
  `.scratch/pod_canon.py`, which diffs our output against `rock1.POD` tag-by-tag.
- [x] **S1c — skinned meshes now always carry a bone-batch table** — report §8e.
  A mesh's 6012 BONEIDX stream is a SLOT index into 6015, whose entries are POD
  **node** indices; `Caver::PODLoader::CreateMesh` dereferences `6016[0]` with no
  null check, so a skinned mesh without 6015/6016 crashed the loader and left the
  model as an empty silhouette in-game (while ruby_gg, which is lenient, drew it
  fine). `pod_writer.cpp` now emits 6018/6019 for every mesh (0/0 when static,
  matching stock and a fresh `PVRGeoPODCLI` export), synthesises the table from
  the game's own “Bone*/Control* + ancestors, ascending node index” marking rule
  when a producer forgot, and pads 6018 so a stray slot cannot index past it.
  Contract pinned by `tests/pod_game_reader_contract_test.cpp` (57 checks).
- [x] **E7 LocalAABB on scale edit — cleared by design** — inspector comment
  (`asset_viewer.cpp:4231-4233`) documents Tag 8 is object-local and excludes scale;
  ground-mesh holders recompute their own AABB. Keep a manual QA pass when editing
  ground meshes after scale changes.

---

## 2. Viewer / renderer glitches (issues.md)

- [x] **A1 · Camera zoom: dynamic clamp replaces the hard 500u ceiling**
  - Doc: `issues.md` §4 Root Cause 1. The fixed `500.0f` clamp at the old
    `asset_viewer.cpp:3397` is gone. Added `apply_model_preview_zoom()` +
    `update_model_preview_clip()` (file scope, `asset_viewer.cpp:2975-2995`) which
    clamp distance to `[max(r*0.01,0.01), max(2000,r*20)]` and drive
    `far_plane = max(2000, distance + r*10)`. Wired into the wheel handler
    (`asset_viewer.cpp:3419`) and all three framing sites (model load, F-key
    reframe, R-key reset). Verified: builds, `bin/ruby` runs.

- [x] **A2 · glTF inverse-bind-matrices imported into PODNode::bind_matrix**
  - Doc: `issues.md` §2 fix 2. `gltf_import.cpp` now reads each skin's
    `inverse_bind_matrices` accessor, inverts it (new `gltf_mat4_inverse`,
    `gltf_import.cpp:142-175`), and stores the bind-pose world matrix into
    `PODNode.bind_matrix` with `has_bind_matrix=true` (`gltf_import.cpp:454-483`).
    The merge path in `pod_loader.cpp:901-906` now preserves an authored bind
    instead of overwriting it with frame-0 eval. **Empirical:** converting
    `minecraft_bee.glb` populates authored binds on all 17 joints (ibm_probe:
    `in-memory has_bind_matrix=17/50`, diagonal shows 0.010 = 1/100 scale
    correctly inverted). Note: `bind_matrix` is a runtime-only field (not
    serialized by `pod_writer`), so this improves in-session skinning of rigs
    posed away from bind at frame 0; the bee's static bbox (921u) is the
    asset's authored scale, not a bug.

- [x] **A3 · glTF texture sampler filtering honoured in the model viewport**
  - Doc: `issues.md` §3 Root Cause 1. `gltf_import.cpp` now marks
    `GLTFPBRInfo::Image::nearest` when the glTF sampler requests NEAREST
    (9728/9984/9986; `gltf_import.cpp:591-617`). The viewer carries the flag via
    a new `gltf_pbr_img_nearest` array + `model_gltf_alias_nearest` map, and
    `load_texture_file` gained a `nearest` param that sets `GL_NEAREST` min/mag
    with no mipmaps/aniso (`asset_viewer.cpp:1781-1841`). Both the PBR spill path
    (`load_slot`) and the alias path (`resolve_model_textures`) honour it.

- [x] **A4 · alpha BLEND/MASK depth + cutoff handling**
  - Doc: `issues.md` §3 Root Cause 3. `PBRMaterial` gained `alpha_mode` +
    `alpha_cutoff` fields (`av_renderer.h:336-341`). `pbr_render_mesh` now:
    OPAQUE → blend off; MASK → blend off + alpha test at `alpha_cutoff` with
    depth write on; BLEND → blend on + `depthMask(false)` (restored after the
    draw). The viewer plumbs `pm.alpha_mode`/`pm.alpha_cutoff` from the glTF
    material into the renderer material.
---

## 3. Ruby editor UX & parity (ruby_parity/ + rotate bug + js-editor docs)

- [ ] **R1 · `rot_y` naming cleanup → `rot_z`**
  - Docs: `ruby_rotate_bug_investigation.md` §2 (field is Z-rotation in 7+ places),
    `js_editor_vs_ruby_editor_investigation.md` §8.2. The behaviour is already
    clarified in the UI tooltip (“In-plane rotation Tag 6 around depth/Z axis”) and a
    separate `model_y_rotation` (“Model Yaw”) exists — but the identifier keeps
    misleading new code readers.
  - Do: rename `SceneObject::rot_y` → `rot_z` across `scene_loader.h`, `scene_workspace*`,
    `scene_player*`, `scene_entity*`, `scene_lua*` + the protobuf comment. (No wire
    change — serialization is by tag number.)

- [ ] **R2 · Decide the entity 0°/180° clamp policy (Lua feedback loop)**
  - Docs: `ruby_rotate_bug_investigation.md` §4/§8; `scene_entity.cpp:528,543,565,573,
    586,609,618` still force `e.rot = (dir<0)?π:0`. This mirrors the *game*'s AI honestly
    (vanilla monsters only face left/right), so removing it changes gameplay fidelity.
  - Do: add a per-entity flag / `CreateObject(..., arbitrary_angle)`-style opt-in that
    lets Lua set a true facing angle, while keeping the default left/right clamp for
    parity. Document the choice in the rotate investigation doc.

- [ ] **R3 · Live particle emitter preview in the scene viewport**
  - Doc: ruby_parity `07` P2/6. Light glow-billboards and water sheets are in
    (`asset_viewer.cpp`, v8.0 Beta 3) but ParticleEmitter emits nothing in the editor.
  - Do: a cheap CPU-emitter preview (spawn/extinguish loop using the component's
    spawn-rate/lifetime/velocity fields) rendered as additive billboards via the
    existing glow-sprite path. No fidelity requirement — editing feedback only.

- [ ] **R4 · Per-light shadow-camera params (near/far/bias/radius/intensity)**
  - Doc: ruby_parity `07` P3/7. `set_point_lights`/`set_directional_lights` don't carry
    them.
  - Do: extend the light struct + Settings → Lighting panel; wire to the renderer's
    shadow-frustum helpers (only where shadows actually render).

### 3.1 Partial / minor
- [~] **R5 · ImGuizmo parity remainder (G1 space toggle, G4 world/last mode)**
  - Doc: ruby_parity `08` §4 (lines 102-155): ImGuizmo is *already* wired; the two
    remaining polish items are the gizmo space (local/world) toggle and giving the
    gizmo its own “last used mode” persistence per-object. Quick win.
- [~] **R6 · GUI remaster leftovers**
  - Doc: ruby_parity `09`. The big items (shared color/slider/drag helpers, path elision)
    are shipped; remaining polish: consistent keyboard focus, the “Collapse all →
    Expand selected” flows, and the 2-col inspector options. Re-read `09` before
    implementing to pick the exact rows.
- [~] **R7 · Texture editor remaster**
  - Doc: ruby_parity `11`. Highest impact left per that doc: undo/redo stack + rectangle
    selection masking for Fill/Erase/tint. Lasso is a bonus.
---

## 4. Animation & skinning parity (more_model_research / CHANGELOG)

- [x] **S1 · Dominant-bone (1-bone rigid) bake for game-faithful skinning**
  - Doc: `more_model_research.md` §6 / §9 ("strictly 1 bone per vertex") / §15.
    **Done 2026-09-06** — `build_pod_from_tg3` gained a `rigid_skin` mode
    (default **on** through the converter's `PodConvertOptions::rigid_skin`, CLI
    opt-out `--smooth-skin`): each vertex is pre-baked to its max-weight joint at
    weight 1.0 (ties → lowest slot), emitting 1-component BoneIndexList/
    BoneWeightList instead of 4. Indices stay in raw `skin.joints[]` space — the
    bone-batch table still does the POD-node remapping, unchanged. The raw
    import APIs (`gltf_import_glb/gltf_import_all_clips`) default to full
    weights so the viewer keeps smooth-skin preview accuracy. FBX is unaffected
    (`fbx_import` drops rigs). Verified on `statue.glb`: rigid POD dumps
    `NumComponents 1` on both bone streams; `--smooth-skin` restores 4.

- [x] **S2 · Resample glTF animation clips to the engine's 24 FPS**
  - Doc: `more_model_research.md` §15 (game hardcodes 24.0 FPS in
    `CreateAnimationFromFile`). **Done 2026-09-06** — `gltf_import_all_clips`
    gained a `target_fps` parameter (0 = legacy key-density derivation; the
    in-code default remains derive-at-30 for library callers), and the converter
    forces it via new `PodConvertOptions::anim_fps` (default **24**, CLI
    `--anim-fps <rate>`, `--anim-fps 0` opts out). All translation/rotation/
    scale channels resample at exactly the target rate (LINEAR/STEP/
    CUBICSPLINE preserved), landing on the engine's frame grid — verified
    reference: `hiro_run.POD` (25 frames @ 24 fps).

- [x] **S1b · Motion-aware choice of the rigid bake's bone**
  - Doc: `pod_fbx_gltf_interconversion_report.md` §8c.1. **Done 2026-09-14** —
    the collapse to one bone per vertex used `argmax(weight)` **at the bind
    pose**, a decision taken from a single instant for a mesh that exists to be
    animated. `refine_rigid_skin()` now scores each vertex's own top-4
    influences against every pose of every clip and keeps the one that tracks it
    best. Same file format, same engine semantics. `soldier.glb`: 12.2 % of
    vertices re-bound, worst-case deviation 15.45 → 11.04; `pilot.glb`: 9.4 %,
    30.46 → 25.46. Falls back to max-weight when no clips exist, so unanimated
    conversions are byte-identical. **Base and clip PODs must be regenerated
    together** (the collapse now depends on the clips). Regression test:
    `tests/rigid_skin_refine_test.cpp` (33 checks over soldier / pilot / statue)
    asserts it is not a no-op, never regresses against smooth skinning, never
    touches the geometry, and is a strict no-op without clips.

- [x] **S2b · Clip browser and converter must agree on fps**
  - Doc: §8c.4. **Done 2026-09-14** — `gltf_inspect_animations` computed the
    span from the max last-key over *all* samplers and fps as a flat 24, while
    the bake used the 90th-percentile span and key-density fps; both now call
    `derive_clip_timing()`. Fixing it exposed the companion path's real problem:
    companion clips are mostly **2-key constant tracks** (70 of 72 for
    `soldier`'s walk), so a median over all tracks derived **1 fps** — a 2-frame
    walk clip. Key density is now measured from tracks with ≥ 4 keys in both the
    GLB and companion paths; all 11 soldier clips derive 30 fps. `--anim-fps 24`
    (engine parity) unchanged; `--anim-fps 0` now means the same thing in both.

- [x] **S4 · POD provenance sidecar (`<file>.POD.meta`)**
  - Doc: §8c.2. **Done 2026-09-14** — the POD format has no version field
    (its only version slot is the engine-parsed `AB.POD.2.0` string), so a stale
    bake is indistinguishable from a fresh one. `pod_stamp.{h,cpp}` writes a
    flat JSON sidecar next to every converted POD, and `pod_load()` warns once
    per path when a POD's revision differs from the build's. Native assets have
    no sidecar and are never mentioned. Bump `SWORDIGO_POD_PIPELINE_REVISION`
    whenever the importer changes what it writes.

- [x] **S5 · `EXT_texture_webp` resolution + libwebp decode**
  - Doc: §8c.5. **Done 2026-09-14** — the extension *replaces* the core
    texture rather than falling back to it, so `textures[i].source` can legitimately
    be absent; `texture_source_image()` now reads
    `extensions.EXT_texture_webp.source`. `image_decode.{h,cpp}` adds an optional
    libwebp path (decode-only), used by all four `pod_convert.cpp` decoder sites
    and both Qt viewer sites. `pilot.glb` converts with **1 texture** where it
    reported "0 textures". Without libwebp the build and behaviour are unchanged.

- [ ] **S3 · `minecraft_bee.glb` end-to-end skinning regression**
  - See A2. After IBM import + dominant-bone bake, convert bee and measure the
    *animated* (frame-0 skinned) bbox — target ≪100u, not 921u.

---

## 5. MCP / CLI future wins (docs/MCP_SERVER.md §5, all straightforward)

- [ ] **M1 · `scene_edit` MCP tools** (move/duplicate/delete objects, set fields) —
  opt-in flag, reuses `scene_workspace` + `scene_save`.
- [ ] **M2 · `ground_mesh` MCP tools** (subdivide/split/extrude) for procedural agents.
- [ ] **M3 · `texture_edit` MCP tools** — extract the Image Editor's CPU pixel ops into
  a module and expose them.
- [ ] **M4 · `lua_run` MCP tool** — evaluate a Lua snippet against scene scripts.
- [ ] **M5 · TCP/WebSocket transport + session daemon** (trivial future extension
  acknowledged in the doc).

---

## 6. Collision authoring for imported models (fix_complex_models_collision.md)

- [ ] **C1 · "Model-to-GroundMesh Collider" pipeline**
  - Doc: `fix_complex_models_collision.md` §5 — fully supported by Caver's design
    (GroundPolygonComponent auto-creates collision; ModelComponent never does). The
    proposed automatic flow (slice on Z≈0 → 2D contour → simplify → emit invisible
    GroundMesh via `boulder::generate_ground_mesh_object`) is **not built**.
  - Do: as a feature (medium size); start with a CLI `bin/ruby --podcollider in.POD
    out.scene` emitting the invisible ground mesh + polygon, reusing existing
    `scene_generator.cpp`/`boulder.cpp` machinery.

---

## 7. Legend / verification method

Items marked `[x]` were verified by at least one of:
1. Reading the current code (exact line) and seeing the described behaviour;
2. A binary comparison / conversion experiment run on 2026-09-05
   (`bin/ruby --glb2pod`, `.scratch/pod_stats`, header parity probe);
3. A CHANGELOG entry describing + regression-testing the fix.

*Last audit: 2026-09-06.*