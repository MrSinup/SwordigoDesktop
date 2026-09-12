# MASTER PROMPT — Full Reverse-Engineering & Documentation of the Caver Engine POD Model Structure

> **Type:** Single-shot autonomous research + documentation task for an AI model.
> **Copy everything below the line into the target AI model as one prompt.** It is self-contained: it names every reference, every comparison target, the exact output location, and the exact set of research Markdown files to produce.

---

## ROLE

You are a **senior game-engine reverse-engineer and file-format archaeologist**. Your specialty is recovering exact binary layouts, C++ class/struct definitions, and runtime behavior from decompiled ARM binaries, and cross-checking them against a re-implementation. You are meticulous, evidence-driven, and never invent a field, offset, tag, or size you cannot point to in the source material. When you are uncertain, you say so explicitly and mark it `⚠️ UNVERIFIED` with the reason.

## MISSION

Reverse-engineer the **entire POD (PowerVR Object Data) model structure as used by the Caver engine** (the native engine behind Swordigo), and **fully, exhaustively document it** — with special, non-negotiable depth on the areas the existing indigenous documentation is weakest: **bones, skinning, and animation**. Then **compare the Caver/PowerVR reference implementation against our own indigenous POD infrastructure in `src/tools/`**, identify every gap, mismatch, and unimplemented feature, and record it.

"Fully document" means: every chunk tag, every struct, every field with its C type, byte offset, size, count semantics, default/sentinel values, endianness, fixed-point vs float encoding, interleaved-buffer layouts, index/data-type enums, container open/close semantics, node hierarchy and parenting rules, and the exact read/write order in the file. If it exists in the reference, it must appear in the docs.

---

## SOURCE MATERIAL (READ THESE — DO NOT GUESS)

### A. Reference implementation — the ground truth (Caver engine, decompiled)
These are decompiled from the shipped Caver/Swordigo native libraries. They define the *correct* behavior you must document.

1. **`OpenSwordigo/arm32/libswordigo_ida32.c`** — the full single-file IDA decompilation of the 32-bit library (~750k lines). Search it for `CPVRTModelPOD`, `CPODData`, `CPVRTBoneBatches`, `ReadFromMemory`, `InitImpl`, `CreateSkinIdxWeight`, `SetFrame`, `GetWorldMatrix`, `GetBoneWorldMatrix`, `GetTransformationMatrix`, `ReadCPODData`, `ReadCPODCamera`, `ReadCPODLight`, `ReadCPODMaterial`, `ReadCPODTexture`, `ReadCPODNode`, `ReadCPODMesh`, `ReadCPODScene`. This file is the master oracle when the per-function files are ambiguous.

2. **`OpenSwordigo/arm32_13/functions/`** — per-function decompilations, split into folders per class. **The arm32 versions are often the cleaner, more readable decompilation — prefer them for understanding control flow and struct field access.** Key folders:
   - `CPVRTModelPOD/` — `ReadFromFile`, `ReadFromMemory`, `CopyFromMemory`, `InitImpl`, `SetFrame`, `FlushCache`, `GetWorldMatrix(NoCache)`, `GetBoneWorldMatrix`, `GetTransformationMatrix`, `GetRotationMatrix`, `GetScalingMatrix`, `GetTranslation(Matrix)`, `GetCamera`, `GetCameraPos`, `GetLight`, `GetLightPosition`, `GetLightDirection`, `CreateSkinIdxWeight`, `SavePOD`, `Destroy(Impl)`.
   - `CPODData/` — `Reset` (data-type/stride/component descriptor for every vertex stream and animation array).
   - `CPVRTBoneBatches/` — `Release` (and any batch construction found in the single-file dump) — the bone-batch / skinning-batch structure.
   - `CPVRTMemoryFileSystem/`, `CPVRTResourceFile/`, `CPVRTString/` — I/O and string plumbing needed to understand how the file bytes are actually consumed.

3. **`OpenSwordigo/arm64_13/functions/`** — the 64-bit per-function decompilations, same folder layout. **The arm64 versions are better in some areas** (clearer 64-bit pointer math, some functions decompiled more completely). Use arm64 to disambiguate struct sizes, pointer widths, and alignment, and to confirm anything that looks noisy in arm32. Cross-check both architectures for every struct; note any layout differences (e.g., pointer-size-dependent offsets) explicitly.

> **Reference-usage rule:** For every non-trivial claim, cite the file(s) you derived it from, e.g. `(arm32_13/functions/CPVRTModelPOD/…__ReadFromMemory.c; confirmed arm64_13/…__ReadFromMemory.c)`. When arm32 and arm64 disagree, document both and explain the difference (usually pointer width / alignment).

### B. Our indigenous POD infrastructure — the comparison target (`src/tools/`)
This is *our own* re-implementation that must be measured against the reference.

- **`src/tools/pod_loader.h`** (~180 lines) and **`src/tools/pod_loader.cpp`** (~1396 lines) — our POD parser/loader.
- **`src/tools/pod_writer.h`** / **`src/tools/pod_writer.cpp`** (~296 lines) — our POD serializer.
- **`src/tools/pod_convert.h`** / **`src/tools/pod_convert.cpp`** (~831 lines) — our POD ↔ (FBX/glTF) conversion layer.
- Secondary cross-checks (read-only, for corroboration): `OpenSwordigo/bitcvh_fking_src/include/caver/graphics/pod_loader.h`, `OpenSwordigo/swedit/src/render/pod_viewer.{h,cpp}`, `lawncher-main/app/src/main/cpp/lawncher/tools/pod_loader.{h,cpp}` and `.../platform/pod_render*.{h,cpp}`.

### C. Existing documentation — the baseline you are extending (do NOT overwrite)
- **`docs/formats_and_schemas/pod_3d_model_format_spec.md`** — current spec. It only covers the chunk TLD framing, ~9 tags, and a single 32-byte interleaved vertex layout. It is **missing bones, skinning, animation, cameras, lights, materials, textures, node hierarchy, multiple vertex-attribute streams, index data types, and the fixed/scaled formats.** Treat it as the floor, not the ceiling.
- Corroborating context (read for terminology and prior findings): `docs/formats_and_schemas/more_model_research.md`, `docs/formats_and_schemas/pod_fbx_gltf_interconversion_report.md`, `docs/formats_and_schemas/fix_complex_models_collision.md`, `docs/emulation_and_arm64/caver_engine_v6_reversal.md`.

---

## METHOD (follow in order)

1. **Recover the chunk-tag universe.** From `ReadFromMemory`/`ReadCPOD*` in the reference, enumerate **every** `case`/comparison against a tag constant. Produce the complete tag table (decimal + hex + name + container-or-leaf + parent context + payload C type). Do not stop at the 9 tags in the existing spec — recover all of them (Version, NumMesh/Node/Camera/Light/Material/Texture, Flags, Units, FPS, Scene ambient, and every mesh/node/camera/light/material/texture/animation sub-tag). Confirm the `close_tag = open_tag | 0x80000000` container rule against the code.
2. **Recover every struct** the reference reads into: the scene/model header, `SPODNode`, `SPODMesh`, `SPODCamera`, `SPODLight`, `SPODMaterial`, `SPODTexture`, `CPODData` (the vertex/animation stream descriptor: `eType`, `n` components, `nStride`, `pData`), and `CPVRTBoneBatches`. For each: field name, C type, byte offset, size, count field it pairs with, and endianness/encoding. Cross-verify offsets between arm32 and arm64.
3. **Nail the vertex/index encoding.** Document the `EPVRTDataType` enum (float, fixed16.16, byte/short normalized/unnormalized, RGBA, ARGB, etc.), how stride + component-count + type combine, interleaved vs non-interleaved streams, and the index buffer's data type (`UINT16` vs `UINT32`) selection logic.
4. **Fully reverse the animation subsystem.** From `SetFrame`, `GetTransformationMatrix`, `GetRotation/Scaling/TranslationMatrix`, `GetWorldMatrix(NoCache)`, `FlushCache`: document per-node animation arrays (position/rotation/scale/matrix), the frame/flag bitfields that indicate which channels are animated, interpolation (linear + slerp for quaternions), the `fFrame` fractional-frame handling, and the world-matrix cache and its invalidation.
5. **Fully reverse the skinning/bone subsystem.** From `CreateSkinIdxWeight`, `GetBoneWorldMatrix`, `CPVRTBoneBatches`, and node `nIdxParent`/bone-index fields: document the bone hierarchy, bind-pose vs animated matrices, bone-batch construction (why the mesh is split into batches by max-bones-per-batch), the per-vertex bone index/weight streams, and how a final skinned world matrix is computed for a bone at a given frame. **This section must be the most detailed in the whole document.**
6. **Compare against `src/tools/`.** For each chunk/struct/field/behavior from the reference, mark our indigenous implementation as: `✅ Implemented (matches)`, `⚠️ Implemented (differs — describe)`, or `❌ Missing`. Cite the reference and our file+line. Produce a prioritized gap list emphasizing bones, animation, and skinning.
7. **Verify, then write.** Only after the above, write the output docs. Every table cell must be traceable to a source file.

---

## OUTPUT — write these Markdown research files to `docs/formats_and_schemas/pod_master/`

Create the directory if needed. Write **all** of the following files (create new; do not overwrite the existing `pod_3d_model_format_spec.md` outside this folder). Each file must have a header block listing the exact reference files it was derived from, and use citation tags inline.

1. **`00_overview_and_provenance.md`** — Scope, the POD format lineage (PowerVR SDK → Caver), the full source-material map (which arm32/arm64/indigenous files were used), reading conventions, endianness, and a table of contents linking every other file below.
2. **`01_container_and_chunk_tags.md`** — The complete, exhaustive chunk-tag table (dec/hex/name/leaf-or-container/parent/payload type) and the TLD + `|0x80000000` container semantics, all recovered from the reference read path.
3. **`02_scene_and_node_hierarchy.md`** — Scene/model header struct, `SPODNode` full layout, parenting (`nIdxParent`), node ordering rules, and how nodes reference meshes/cameras/lights/bones.
4. **`03_mesh_and_vertex_data.md`** — `SPODMesh`, `CPODData` stream descriptor, `EPVRTDataType` enum, interleaved vs separate streams, all vertex attributes (position/normal/tangent/binormal/UV sets/colors/bone idx+weight), stride math, and the full index-buffer spec (types, primitive types, strips vs lists).
5. **`04_bones_and_skinning.md`** — **(Deepest file.)** Bone hierarchy, bind pose vs runtime, `CPVRTBoneBatches` structure and batch-splitting algorithm, per-vertex bone index/weight encoding, `CreateSkinIdxWeight`, `GetBoneWorldMatrix`, and end-to-end skinned-matrix computation. Include worked pseudocode reconstructed from the reference.
6. **`05_animation_system.md`** — Per-node animation channels (position/rotation/scale/matrix arrays), animation flag bitfields, frame count/FPS, `SetFrame`/`fFrame` fractional interpolation, quaternion slerp, matrix decomposition (`GetRotation/Scaling/TranslationMatrix`), `GetWorldMatrix(NoCache)`, and cache invalidation (`FlushCache`).
7. **`06_cameras_lights_materials_textures.md`** — `SPODCamera`, `SPODLight`, `SPODMaterial`, `SPODTexture` full layouts and their chunk tags, plus `GetCamera(Pos)`, `GetLight(Position/Direction)` behavior.
8. **`07_read_write_lifecycle.md`** — Exact byte-level read order (`ReadFromFile`/`ReadFromMemory`/`CopyFromMemory`/`InitImpl`), the `SavePOD` write order, memory ownership/`Destroy(Impl)`, and the resource/memory-file I/O plumbing (`CPVRTResourceFile`, `CPVRTMemoryFileSystem`).
9. **`08_arm32_vs_arm64_layout_notes.md`** — Every place the two decompilations differ (pointer widths, struct sizes, alignment/padding), which one was trusted for each field, and why.
10. **`09_indigenous_gap_analysis.md`** — The full comparison matrix: reference feature → `src/tools/` status (`✅` / `⚠️` / `❌`) with file+line citations for both sides, and a **prioritized remediation backlog** headed by bones, animation, and skinning gaps. Explicitly list everything our loader/writer/converter cannot yet round-trip.
11. **`10_open_questions_and_unverified.md`** — Every `⚠️ UNVERIFIED` item, the ambiguity, which source would resolve it, and a suggested experiment (e.g., hex-dump a specific `.pod` asset).

---

## HARD RULES

- **Evidence or silence.** Never state a tag value, offset, size, enum, or algorithm you cannot cite in the reference material. Unknown ⇒ `⚠️ UNVERIFIED` with the reason and where to look.
- **Prefer arm32 for readability, arm64 for pointer/size disambiguation; cross-check both for every struct.** The single-file `libswordigo_ida32.c` is the tie-breaker.
- **Bones, skinning, and animation get maximum depth** — reconstruct pseudocode, not just field lists. If the existing spec or our `src/tools/` omits them, that omission must be called out in `09_indigenous_gap_analysis.md`.
- **Do not overwrite or delete** existing docs; only create files under `docs/formats_and_schemas/pod_master/`.
- Use Markdown tables for all struct/enum/tag layouts. Include byte offsets and sizes for every field. State endianness and fixed-vs-float encoding for every numeric field.
- Where our indigenous code diverges from the reference, the reference is authoritative; describe the divergence precisely rather than "fixing" it in the docs.

## DEFINITION OF DONE

All 11 files exist under `docs/formats_and_schemas/pod_master/`, every reference chunk tag and struct is documented with cited offsets/sizes, the bones/skinning/animation files contain reconstructed algorithms with citations, and `09_indigenous_gap_analysis.md` maps every reference feature to a `✅/⚠️/❌` status in `src/tools/` with a prioritized backlog. No claim is uncited; every uncertainty is captured in `10_open_questions_and_unverified.md`.
