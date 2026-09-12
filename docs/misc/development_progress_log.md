# Progress Log

## 2026-06-14 - Agent 1

- Read `prompt/Phrase 1/main.md` and `prompt/Phrase 1/preFeed.md` before investigation.
- Read shared research files; they were empty at the start of the Agent 1 pass.
- Mapped Vita loader boot sequence from `so_file_load` through relocation/import resolution, fake JNI setup, direct JNI lifecycle calls, and the frame loop.
- Mapped Android Java startup sequence from `System.loadLibrary` through `setFilesDir`, `setCacheDir`, `setAssetManager`, `setupNativeInterface`, `setupApplication`, `setApplicationViewSize`, `updateApplication`, and `drawApplication`.
- Confirmed local `libswordigo.so` is ARM ELF32 and cannot be `dlopen`ed natively on x86_64 Linux.
- Confirmed local native exports include the core startup/frame JNI functions but do not include `handleApplicationLaunch` or `googleSignInCompleted`; documented version mismatch risk with the Vita loader source.
- Cataloged native imports for Android asset APIs, GLES/EGL, OpenAL, logging, libc, pthread, and zlib.
- Ran a small asset-format experiment with `file`, `xxd`, and `protoc --decode_raw` on `.gdata`, `.gstate`, and `.scene`; documented results in `research/experiments/experiment_001.md`.
- Attempted ARM disassembly with host `objdump`; it failed because this environment lacks an ARM-capable objdump.

## 2026-06-14 - Repair after Agent 2 overwrite

- Restored evidence-based `findings.md`, `protobuf_schema.md`, `hypotheses.md`, `questions.md`, `progress_log.md`, and `agent_messages.txt` after Agent 2 replaced them with speculative summaries.
- Preserved verified Agent 2 direction only as cautious Lua/program scripting notes: `assets/resources/plains_woodkeep3.scene` contains readable Lua source and `LuaQ` chunks, and native symbols include `Caver::Program*` and related components.
- Reclassified unverified claims about field `158`, `.scl` schema, item type mappings beyond sampled values, quests, and world triggers as open questions until backed by decode evidence.

## 2026-06-14 - Agent 2 (Asset Evidence)

- Re-verified asset findings with command evidence to supplement the research baseline.
- Confirmed `.scl` files as protobuf entity template collections via `protoc --decode_raw < "assets/resources/monsters.scl"`.
- Identified specific protobuf fields for Lua source/bytecode in `.scene` files via `protoc --decode_raw < "assets/resources/plains_woodkeep3.scene"`.
- Documented quest and trigger structures in `gamedata.gdata` message types 3 and 5.

## 2026-06-14 - Agent 2 (Mission 2: Linux Loader & Native Analysis)
- Analyzed Vita loader source to design a Linux-compatible ARMv7 loader.
- Mapped native engine symbols (`Caver::*`) from `libswordigo.so`.
- Investigated `.POD` asset pipeline, confirming separate animation files and external `.pvr` textures.
- Produced `research/linux_loader_design.md`, `research/native_symbol_map.md`, and `research/pod_pipeline.md`.
- Determined the minimum viable JNI and Android API set to reach `setupApplication()`.

## 2026-06-14 - Agent 2 (Mission 5: Version Alignment)
- Compared Swordigo 1.1 and 1.4.6 native libraries and Java source.
- Identified that 1.4.6 adds 13 JNI exports, including `handleApplicationLaunch`.
- Confirmed that the Vita port reference aligns perfectly with 1.4.6's startup sequence.
- Mapped major engine subsystems in 1.4.6 (`Caver` namespace).
- Recommended 1.4.6 as the primary development target for the Linux port.

## 2026-06-14 - Agent 2 (Mission 6: Boot Prototype Execution)
- Integrated **Unicorn Engine** for ARMv7 emulation on x86_64 host.
- Implemented **Magic LR Stop Condition** and memory-mapped bridge addresses.
- Developed `resolve_all_to_bridge` to automatically stub all external library dependencies.
- **SUCCESS**: Executed `handleApplicationLaunch` and `setupNativeInterface` successfully.
- Currently debugging `setupApplication` memory fetch and JNI return logic.

## 2026-06-14 - Agent 2 (Mission 7: Breaking through setupApplication)
- Overhauled JNI bridge with functional callbacks and a guest heap allocator.
- Replaced root library with version 1.4.6 and verified symbols.
- **SUCCESS (Tier 2)**: Reached and returned from `setupApplication()`.
- **SUCCESS (Tier 4)**: Reached and executed over 1 million instructions in `drawApplication()`.
- Identified memory pressure (> 1GB) and uninitialized pointers as the next hurdles.

## 2026-08-04 - Ruby Native Scene Renderer Remaster

- Used `OpenSwordigo/resources/Ruby/src/main/libswordigo_arm32.c` as the primary behavior/schema reference; did not modify `OpenSwordigo/`.
- Restored the interrupted Ruby editor patch and verified the `ruby` CMake target builds again.
- Added native scene object editing primitives in `src/tools/scene_loader.h/.cpp`: fresh `objN` identifiers, create, duplicate, delete, reorder, metadata refresh, and atomic scene replacement.
- Added Ruby scene object controls and bounded undo/redo snapshots in `src/tools/asset_viewer.cpp`.
- Fixed the blank native scene viewport's parser/render pipeline:
  - `ModelComponent.Name` is now decoded from the component's nested payload selected by dynamic `type_id`, matching `libswordigo_arm32.c`.
  - POD lookup accepts schema-style model names with or without `.pod` and searches scene/resource/model paths.
  - Embedded GroundMesh GPU upload is now called when a scene is opened and after text recompilation.
  - GroundMesh wrapper decoding now uses the component's actual `type_id` instead of hard-coded field `111`.
  - GroundMesh `Mesh`, `SurfaceMesh`, and `FrontMesh` fields (4, 5, 6) are all parsed.
  - Mesh face counts are converted to index counts; material is read from field 10; vertex/index blobs remain fields 50/51.
- Fixed scene transform rendering: `.scene` rotation radians are converted to degrees before calling the native matrix helper.
- Disabled back-face culling in the authoring viewport so mixed POD/GroundMesh winding cannot hide complete assets.
- Remastered scene visibility and navigation:
  - Bounds-aware initial camera framing with dynamic near/far clipping.
  - `F` frames the complete scene while the viewport is hovered.
  - Unresolved/component-only objects render as orange proxy boxes instead of disappearing; selected proxies are blue.
  - Viewport overlay reports rendered object and proxy counts and shows mouse controls.
  - Hidden objects can be included/excluded through Render Settings.
  - Grid shader now receives the model transform and renders world-space minor/major lines instead of evaluating an unscaled unit quad.
- Fixed the scene mode switcher:
  - Removed continuous `SetSelected` forcing that overrode user tab clicks.
  - Programmatic selection is now a one-frame synchronization after scene load.
  - Scenes open in the 3D visual editor by default; Editor and Inspector remain selectable.
- Fixed the folder chooser/path bar:
  - Path text now persists while typing instead of being reconstructed from `current_dir` every frame.
  - `~` expansion and canonical directory navigation are supported.
  - Invalid folders produce a visible status error.
- Verification:
  - `cmake --build build-cmake --target ruby -j2` passes.
  - `timeout 5s ./bin/ruby` starts successfully.
  - Runtime OpenGL output confirms both shaders compile/link: `[av_renderer] Initialized (model prog=3, grid prog=6)`.
- Existing unrelated startup warning remains: IntelliJ/BatSyntax regex patterns report three `std::regex` errors. This was not part of the scene renderer pass.
- Remaining `libswordigo_arm32.c` parity work, to implement incrementally:
  - Viewport raycast selection and translate/rotate/scale transform gizmos.
  - Schema-driven typed component editor with add/remove component operations.
  - Clipboard copy/paste and complete undo snapshots for every field edit.
  - GroundPolygon/collision authoring and editable GroundMesh geometry/materials.
  - Scene libraries, groups, bounds, scene-level scripts, and richer background/material rendering.

## 2026-08-04 - Ruby Scene Interaction and Component Parity Pass

- Continued sequentially without subagents and kept `OpenSwordigo/` read-only.
- Implemented native viewport object picking modeled on `libswordigo_arm32.c::doPick`:
  - Projects object positions through the native perspective camera.
  - Selects the nearest visible object inside a depth-scaled screen-space radius.
  - Clicking empty viewport space clears selection.
  - Inspector and viewport selection now share the same synchronization path, including pending OnLoad edits.
- Added native viewport transform modes mirroring the reference TransformControls workflow:
  - Navigate, Move, Rotate, and Scale toolbar modes.
  - Numeric shortcuts `1` through `4`; `Esc` returns to navigation.
  - Move operates along camera-right/camera-up screen axes.
  - Rotate modifies Swordigo's stored Y rotation in radians.
  - Scale preserves Swordigo's uniform scale constraint.
  - Transform drag begins with an undo snapshot and refreshes scene bounds while editing.
- Added safe component structure operations modeled on `libswordigo_arm32.c::addComponent` and `removeComponent`:
  - Component type chooser is populated from the generated `Component` schema.
  - Add creates class name, fresh object-local component identifier, and the correct nested payload field.
  - Remove deletes a selected component without disturbing neighboring components.
  - Component panels now display generated schema field names, field numbers, and nested-message status.
- Corrected an important component schema bug:
  - A component's field 2 value is its instance identifier, not its payload field number.
  - Model and GroundMesh payload fields are now resolved from `g_schemas["Component"]` (`ModelComponent` field 101, `GroundMeshComponent` field 111, etc.).
- Verification repeated:
  - `cmake --build build-cmake --target ruby -j2` passes.
  - `timeout 5s ./bin/ruby` starts and initializes model/grid OpenGL shader programs.
- Next parity slices remain typed protobuf value mutation, clipboard copy/paste, template library materialization/unlinking, accurate mesh-triangle raycasting, transform axis gizmos, and scene-level structures.

## 2026-08-04 - Ruby Typed Fields, Clipboard, and Scene Script Pass

- Added preservation-safe component payload decoding and mutation:
  - Payload fields are decoded from the schema-resolved nested component message.
  - Repeated field occurrences are tracked independently.
  - Varint, float/fixed32, double/fixed64, and printable string fields are editable.
  - Mutating one value re-emits all unknown wrapper and payload fields unchanged.
  - Nested messages and opaque/binary byte fields remain read-only to avoid corrupting unsupported formats.
- Replaced schema-name-only component panels with actual payload values and typed controls.
- Added native object clipboard operations modeled on `libswordigo_arm32.c`:
  - Copy selected object.
  - Paste copied object with a fresh `objN` identifier.
  - Existing duplicate/delete/reorder and undo integration remain available.
- Added native component clipboard operations:
  - Copy any component, including unsupported raw payload fields.
  - Paste into another object while assigning a fresh object-local component identifier.
  - Raw payload and unknown fields are preserved.
- Added scene-level OnLoad management:
  - Inspect scene metadata counts for object libraries, bounds, groups, unknown fields, and OnLoad scripts.
  - Create, select, edit, and remove scene OnLoad Lua source.
  - Program mutation preserves non-source protobuf fields.
- Consolidated the global `R` scene-camera reset through the same bounds-aware `frame_scene_camera()` used by viewport framing.
- Verification:
  - `cmake --build build-cmake --target ruby -j2` passes.
  - `timeout 5s ./bin/ruby` initializes the actual SDL/OpenGL renderer and both shaders.
- Remaining major parity areas are nested-message editors (vectors/colors/programs/materials), template library materialization/unlinking, editable groups/bounds/library records, accurate triangle raycasting, visible transform-axis gizmos, and direct GroundPolygon/GroundMesh geometry authoring.

## 2026-08-04 - Scene Mode Switcher Full Repair

- Audited every `scene_preview_tab` read/write and removed the ImGui `BeginTabBar` implementation entirely.
- Root causes found:
  - The visible switcher lived in the narrow right properties pane while the switched content lived in the center pane, making state changes appear disconnected or absent.
  - ImGui tab activation state and application mode state were bidirectionally assigned each frame, allowing stale tab state to overwrite button clicks.
  - The structured editor and source editor shared one dirty flag even though their in-memory documents are separate.
  - The save button compiled source only based on the currently displayed mode, while `Ctrl+S` always saved the structured scene, causing source edits to be silently ignored.
  - Pressing `Esc` in Visual transform mode could both cancel the tool and close Ruby.
  - Source compilation reloaded CPU scene data without consistently rebuilding every GPU cache, selection state, history state, and camera frame.
- Replaced the tab bar with a single authoritative segmented switcher at the top of the center scene workspace:
  - `Source`, `Visual`, and `Tree` are always visible next to the content they control.
  - Exactly one integer state controls dispatch and highlighting.
  - No ImGui tab internals can overwrite the selected mode.
- Added explicit source/structured synchronization:
  - Source has its own `scene_text_dirty` state.
  - Entering Source from Visual/Tree regenerates markup from the current structured scene when source has no independent edits.
  - Leaving dirty Source preserves its text and displays a warning that Visual/Tree still represent the last compiled scene.
- Unified source compilation for the Save button and `Ctrl+S`:
  - Both use one atomic temporary-file compilation path.
  - Successful compilation reloads the scene, clears selection/history, rebuilds POD textures/meshes and GroundMesh GPU resources, and reframes the camera.
  - Compilation errors remain in Source and do not replace the original scene file.
- Fixed `Esc`: it now exits a Visual transform tool first; a subsequent `Esc` closes Ruby.
- Added `scene_serialize()` so structured edits can be rendered into Source markup without writing to disk.
- Verification:
  - `cmake --build build-cmake --target ruby -j2` passes.
  - Runtime smoke test starts Ruby and initializes both OpenGL shader programs.
  - Search confirms the old scene `BeginTabBar`, `SetSelected`, and `scene_tab_sync` paths are gone.

## 2026-08-04 - Scene Rendering and Selection Subsystem Remaster

- Re-audited the actual `libswordigo_arm32.c` scene renderer (`showScene`, `buildObject`, `addModel`, `addGroundMesh`, `qd`, `doPick`) rather than relying on generated names alone.
- Fixed the core reason many scenes still rendered blank:
  - Shipped component class names use short names such as `Model` and `GroundMesh`, while generated schema classes use `ModelComponent` and `GroundMeshComponent`.
  - Native dispatch now follows `libswordigo_arm32.c::Ot`: the first nested component message at field >= 50 is authoritative.
  - `SceneComponent` records its real payload field independently from its object-local identifier.
- Corrected GroundMesh parity against `libswordigo_arm32.c::addGroundMesh`:
  - Mesh fields are 6, 8, and 9, not 4, 5, and 6.
  - Native packed fallback is position3/normal3/uv2, eight float32 values and 32 bytes per vertex.
  - Index data defaults to packed uint16 when MeshData metadata is absent.
  - Existing metadata-driven layouts remain supported.
- Implemented embedded ObjectLibrary template inheritance:
  - Parses ObjectTemplate records from Scene.ObjectLibrary.
  - Resolves inherited components for objects with a TemplateName.
  - Merges local component overrides by object-local component identifier, matching the reference editor.
  - Applies template scaling only to rendering/bounds and never serializes it into object scale.
- Corrected Swordigo world orientation to match `libswordigo_arm32.c::qd`:
  - Scene object rotation is around Z, not Y.
  - Scene grid is rendered on the XY plane at scene depth.
  - Initial camera frames the scene directly along the Z-forward axis.
- Replaced point-radius picking with camera-ray/bounding-volume picking:
  - POD model bounds and GroundMesh bounds determine selectable volume.
  - Nearest ray hit wins.
  - Proxy-only objects remain selectable with a stable minimum radius.
- Added explicit right-panel selection status and reliable `Show in Visual` / `Show in Tree` navigation.
- Added case-insensitive recursive POD resolution comparable to `libswordigo_arm32.c` resource lookup, while retaining fast direct candidate paths.
- Added scene render diagnostics for resolved model objects, GroundMeshes, and inherited template objects.
- Runtime evidence now shows real scene parsing and mass texture resolution during smoke testing, including `fire_part2.scene` and `florennum_jail_part1.scene`, instead of a shader-only startup.

## 2026-08-04 — Launcher FFmpeg symbol crash fixed + launcher ruby resolution hardened
- Root-caused `./run_swordigo.sh` crash: `bin/swordfare: symbol lookup error: bin/libs/libswgfx.so: undefined symbol: avformat_open_input` right after the menu background video was found.
- `libswgfx.so` (which contains `video_background.cpp`) statically references 22 FFmpeg symbols, but FFmpeg static archives were linked only into the `swordfare_boot` executable, which never references FFmpeg itself — so the linker never pulled the archives (compounded by `-Wl,--allow-shlib-undefined`).
- Fix: keep FFmpeg in the executable and force-include/export it:
  - Root `CMakeLists.txt`: wrapped `${FFMPEG_LIB_DIR}/lib{avformat,avcodec,swscale,avutil}.a` in `-Wl,--whole-archive ... -Wl,--no-whole-archive` on `swordfare_boot` (which already uses `-rdynamic`), so `libswgfx.so`'s imports resolve from the executable at load.
  - `Makefile`: same `--whole-archive` wrap around `$(DYNARMIC_STATIC_LIBS)` + `$(FFMPEG_STATIC_LIBS)` in the `bin/swordfare` link.
  - Reverted attempts to link FFmpeg into the shared `libswgfx.so` — the FFmpeg build lacks `-fPIC` (`R_X86_64_32` relocation error).
- Verified: `nm -D bin/swordfare` now exports `avformat_open_input`, `avcodec_open2`, `av_read_frame`, `sws_getContext` (809 `av_`/`sws_` symbols); full `cmake --build build-cmake -j4` is green; `timeout 45s ./bin/swordfare` boots to menu, loads `menu_back_2x.mp4` video background, registers the decoder, and completes a 353-frame game session with no symbol errors.
- Also hardened all Ruby-launch sites in `src/platform/launcher_ui.cpp` (`execlp("./ruby", ...)` at the old lines 1296 and 1948) into a shared `launch_ruby_viewer()` helper that resolves `ruby` next to `/proc/self/exe` before falling back to PATH, matching the pattern already in `src/platform/launcher.cpp` (754–769).
- Confirmed `bin/ruby` (16:29:36, 14919000 bytes) was NOT clobbered by the full build — it still contains the remastered scene/switcher strings and initializes cleanly.

## 2026-08-04 — POD remaster baseline (from libswordigo_arm32.c)
- Reference decoder is `libswordigo_arm32.c` `Is` (39571) + `xS` (39616) + `Sp` (39592) + `Lr` (39539) + `Mp` (39484) + `Ge` (39436).
- `yS` (mesh): reads `meshNumVertices/NumFaces/NumUVWChannels`, `meshInterleavedDataList` raw bytes, then `Ls` for vertex/normal/UVW/boneIdx/boneWgt (interleaved offset in the 4-byte blockData payload when stride>0); indices are decoded via `Is({...c,n:1,stride:Sp(dataType)}, numFaces*3, 1)` and widened into `new Uint32Array(numFaces*3)`.
- `bS` (node): prefers anim variants (`nodeAnimPosition ?? nodePosition`, etc.), scale default is the 7-component `[1,1,1,0,0,0,0]`; matrix from `nodeAnimMatrix ?? nodeMatrix`.
- `MS` (material): name (3000), diffuseTextureIndex (3001), opacity (3002), diffuse (3004).
- `Ya` (scene): fps from `sceneFPS` (2017, default 30), numFrames (2009), numMeshNodes (2006).
- Current `src/tools/pod_loader.cpp` diverges from this: indices truncate to uint16, interleaved offset detection differs, node parser uses static-transform flags (`is_old_format`) that libswordigo_arm32.c does not, scale stride heuristic is ad hoc, and materials omit opacity/diffuse.

## 2026-08-04 — POD remaster applied (index widening + materials + interleaved stride)

Implemented the `libswordigo_arm32.c` POD remaster baseline from the previous entry:

- **Indices widened to uint32**: `PODMesh::indices` is now `std::vector<uint32_t>`. Updated
  `pod_loader.cpp::parse_indices` (decodes by dataType via `Sp` semantics, sized `numFaces*3`,
  never truncates), `scene_loader.cpp` ground-mesh index decode (dropped the `(uint16_t)` truncation
  on the u32 case), all three `asset_viewer.cpp` upload sites, `av_renderer.h/.cpp::upload_mesh`
  signature (takes `const uint32_t*`, EBO sized `* sizeof(uint32_t)`), and the draw path now uses
  `glDrawElements(..., GL_UNSIGNED_INT, ...)`. The static grid-plane quad in `av_renderer.cpp`
  (lines 404/788/817) stays uint16 — it is not model data. The proxy-cube indices in
  `asset_viewer.cpp` were promoted to `uint32_t`.
- **Material opacity/diffuse**: added `PODMaterial::opacity` (3002, default 1.0) and `diffuse[3]`
  (3004, default [1,1,1]); parsed in `readMaterialBlock`; both preview render sites now pass
  `[diffuse, opacity]` as RGBA into `render_mesh` (material color, with the existing highlight
  override preserved).
- **Interleaved stride fallback**: `unpack_vertex_data` now computes the default stride from the
  block's `num_components` (not the requested component count) and reads only
  `min(block_components, requested)` components — mirroring `libswordigo_arm32.c::Is`/`Ls` exactly.
- Verified node anim-preferring logic and scale stride heuristic (3 vs 7) already match both
  `libswordigo_arm32.c::bS`/`$c` (frame-0 preview semantics) and the OpenSwordigo native loader; left as-is.

Build: `cmake --build build-cmake --target ruby` succeeds. Smoke test: `timeout 8s ./bin/ruby`
initializes `[av_renderer]`, loads real POD models (balloffire 181v, bed 348v, bookshelf 768v,
crack 921v, corrupted_skeleton 877v), textures decode via PVR path, no crashes. One pre-existing
failure: `boss1_shadowform_spin.POD` (animation-only merge case) still fails to resolve its base
model during the smoke test — unrelated to this change.

Follow-up animation work completed in the same pass:

- `get_node_matrix` now takes a fractional frame and implements the engine's `SetFrame` behavior:
  integer/next frame selection plus fractional interpolation. Translation and scale use vec3 lerp;
  rotation uses shortest-path quaternion slerp; matrix animation remains discrete, matching the
  engine's `GetWorldMatrixNoCache` path.
- `nodeAnimFlags` bits are honored exactly as confirmed from IDA (`1` translation, `2` rotation,
  `4` scale, `8` matrix). A present stream without its animation bit supplies the static frame-0
  transform, matching the native getters.
- Sparse `nodeAnim*Index` arrays now remap each scene frame to the corresponding float offset for
  both endpoints. Dense streams retain the native 3/4/7/16-float frame strides.
- Ruby's animation timeline is now fractional (`SliderFloat`, continuous `dt * FPS` playback), and
  cached scene PODs use the same selected frame instead of being hard-coded to frame zero.

IDA verification: `CPVRTModelPOD::GetTranslationMatrix` `0x505560`, `GetRotationMatrix`
`0x50529c`, `GetScalingMatrix` `0x505364`, and `GetWorldMatrixNoCache` `0x50515c`. Rebuilt `ruby`
successfully and repeated the 8-second runtime smoke test; real multi-mesh PODs and PVR textures
loaded with no parser, animation, or OpenGL errors.

## 2026-08-04 — ARM32/libswordigo_arm32.c accuracy pass for broken POD and scene views

Compared the native viewer to `OpenSwordigo/arm32/libswordigo_ida32.c` and the current Ruby
`libswordigo_arm32.c`, then corrected several root causes rather than adding display workarounds:

- **POD node transforms:** static position/rotation/scale/matrix tags now set only their own
  presence flags. The old parser set `has_matrix` whenever *any* static transform tag appeared,
  causing a synthetic identity matrix to override valid TRS on many models. Quaternion conversion
  now matches `libswordigo_arm32.c::$c` by negating POD xyz before composing the matrix.
- **Animation compatibility:** animation streams with `animFlags == 0` are treated as dense animated
  streams (required by older exporters), while nonzero flags retain engine bit semantics. Scene FPS
  tag 2017 is parsed and becomes the viewer playback default. ARM32 evidence used:
  `SetFrame` `0x33D85E`, `GetWorldMatrixNoCache` `0x33D890`, `GetRotationMatrix` `0x33D974`,
  `GetScalingMatrix` `0x33DA00`, and `GetTranslationMatrix` `0x33DAFC`.
- **Animation-only model resolution:** base lookup now strips suffixes from the right, longest first,
  so `boss1_shadowform_spin.POD` correctly merges with `boss1_shadowform.POD` instead of incorrectly
  searching only for `boss1.POD`. Verified with the shipped asset: 2 meshes, 29 nodes, 12 frames,
  flags 7. `bladeblob_attack.POD` and `bladeblob_move.POD` also merge and load successfully.
- **Model origin and framing:** world-space mesh bounds are now computed through node hierarchy;
  `CenterPoint` is detected and applied with the same root offset as `libswordigo_arm32.c::qa`. Cached scene
  models use the same centering path.
- **GroundMesh scene corruption:** removed the heuristic MeshData interpretation. Per
  `libswordigo_arm32.c::addGroundMesh`, field 50 is always decoded as exactly 32-byte interleaved
  position3/normal3/uv2 records using field 1 as vertex count, and field 51 as uint16 indices.
  Mesh order now matches libswordigo_arm32.c: FrontMesh(8), SurfaceMesh(9), Mesh(6). This fixes exploded/cursed
  scene geometry caused by stale metadata and incorrect face-count assumptions.
- **Renderer state:** `end_3d()` now restores blend, depth-write, polygon mode, and shader state so
  the embedded 3D pass cannot corrupt subsequent ImGui/source/tree rendering.

Verification:

- `cmake --build build-cmake --target ruby -j2` succeeds.
- Direct scene probes: `fire_part1.scene` = 93 objects, 151 ground meshes, 3807 vertices, 5688
  indices; `florennum_cave1.scene` = 97 objects, 79 ground meshes, 2132 vertices, 2970 indices.
- Runtime scene traversal loaded large shipped scenes with up to 163 ground meshes and no parser or
  OpenGL errors. Runtime model traversal loaded normal and animation-only PODs without failures.

## 2026-08-04 — POD skeletal animation and animation diagnostics

Implemented the missing deformation stage that prevented character POD animations from visibly
moving even though node keyframes were being sampled correctly:

- Added `skin_mesh()` to the standalone POD module. It computes bind-pose and current node world
  matrices, resolves per-mesh bone-batch palette indices, builds bind-relative bone transforms,
  skins positions and normals, and normalizes output normals. The matrix order follows IDA
  `CPVRTModelPOD::GetBoneWorldMatrix` (`0x505874`): mesh bind world, inverse bone bind world,
  animated bone world, then mesh-local conversion.
- Added dynamic VBO updates through `av_renderer::update_mesh_vertices`; both the standalone model
  viewport and scene-cached model path now upload CPU-skinned geometry for the active fractional
  frame before drawing.
- Added an optional **Show Skeleton** overlay to the model inspector, rendering the live parent-child
  hierarchy at the active frame. This is both a new viewer feature and a practical animation
  diagnostic.
- Kept POD material textures, center-point offset, hierarchy transforms, fractional TRS animation,
  and skin deformation composable instead of replacing one another.

Verification against shipped character assets:

- `hiro.POD`: 5 skinned meshes, 61 nodes, CenterPoint present. Frame-zero CPU skinning differs from
  source bind vertices by at most `7.63e-06`, validating bone palette mapping and matrix order.
- `hiro_run.POD`: correctly merges with `hiro.POD`, 25 frames, animation flags 7. Half-animation
  frame deformation moves individual meshes by 2.26 to 38.40 units while preserving the frame-zero
  bind-pose invariant.
- Bone palettes verified on `hiro`, `npc_male1`, `ghostlord`, and `dragonkin`; shipped assets use
  palette-local bone indices mapped through tag 6015 as expected.
- Rebuilt `ruby` successfully and repeated mixed scene/model runtime traversal with no parser,
  skinning, OpenGL, or animation-only merge errors.
