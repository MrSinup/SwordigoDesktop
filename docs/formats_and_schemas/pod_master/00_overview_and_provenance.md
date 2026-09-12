# 00 — Overview & Provenance

> **Derived from:**
> - `OpenSwordigo/arm32_13/libswordigo_ida.c` (single-file IDA decompilation, 716,563 lines — master oracle)
> - `OpenSwordigo/arm32_13/functions/CPVRTModelPOD/*` and `.../CPODData/*` (per-function decompilations)
> - `OpenSwordigo/arm64_13/libswordigo_ida.c` and `.../functions/CPVRTModelPOD/*` (64-bit cross-check)
> - `src/tools/pod_loader.{h,cpp}`, `pod_writer.{h,cpp}`, `pod_convert.{h,cpp}` (indigenous implementation)
> - `docs/formats_and_schemas/pod_3d_model_format_spec.md` (baseline spec being extended)

## Scope

This folder is the exhaustive, evidence-cited reverse-engineering of the **POD (PowerVR Object Data) model format** as consumed by the **Caver engine** (the native engine behind Swordigo). It documents every chunk tag, struct, field offset, encoding, and the read/write/animation/skinning behaviour recovered from the decompiled `libswordigo` binaries, and compares that ground truth against our own `src/tools/` loader, writer, and converter.

## Format lineage

POD is Imagination Technologies' PowerVR SDK model container. Caver embeds a build of the PowerVR `CPVRTModelPOD` reader. The on-disk identifier string recovered from the reference is **`"AB.POD.2.0"`** (read at tag `0x3E8`; `arm32_13/libswordigo_ida.c` `sub_3BF6A0`, `case 0x3E8u`, `strcmp(s1, "AB.POD.2.0")`). This corresponds to the PowerVR POD 2.0 line.

## Reading conventions used in these docs

- **Tag values** are given in decimal and hex. Container/open tags are compared directly; the matching **close tag = `open | 0x80000000`** (verified below).
- **Offsets** are byte offsets into the corresponding C struct, derived from the decompiled pointer arithmetic. Where the decompiler expresses an offset in DWORD units (e.g. `a1 + 9`), the byte offset is `9 * 4 = 36`.
- **Encoding**: unless stated otherwise, scalar fields are little-endian; the reader explicitly rejects big-endian files (see below).
- Every non-trivial claim carries an inline citation of the form `(arm32_13 sub_3BF6A0 case 0x7DC)` or a filename.
- Anything not directly observable in the source is tagged **⚠️ UNVERIFIED** with the reason and where to resolve it (collected in `10_open_questions_and_unverified.md`).

## Endianness

The master reader (`sub_3BF6A0`, the target of `ReadFromMemory`/`ReadFromFile`) contains an explicit endianness sentinel:

```c
if ( v172 != -402456576 )   // 0xE7 FFFF 00-ish endian-flipped marker
    goto LABEL_23;
PVRTErrorOutputDebug("Error: Endianness mismatch between the .pod file and the platform.\n");
return 1;
```
*(arm32_13 `sub_3BF6A0`, `default:` of the outer switch.)* Interleaved buffers are additionally byte-swapped only on big-endian hosts via `PVRTFixInterleavedEndianness` → `PVRTIsLittleEndian` (which on this build always returns little-endian). See `03_mesh_and_vertex_data.md`.

## Top-level read entry points

| Function | Address (arm32_13) | Role |
|---|---|---|
| `CPVRTModelPOD::ReadFromFile` | `0x3BF618` | Opens a `CSourceStream` on a path, delegates to `sub_3BF6A0`. |
| `CPVRTModelPOD::ReadFromMemory(char const*,uint,...)` | `0x3C0240` | Opens a `CSourceStream` on a memory buffer, delegates to `sub_3BF6A0`. |
| `sub_3BF6A0` | `0x3BF6A0` | **The master read loop** — the tag switch that decodes the whole file. |
| `CPVRTModelPOD::ReadFromMemory(SPODScene const&)` | `0x3C02BC` | In-memory copy of an already-parsed `SPODScene` (`qmemcpy 0x54`, `InitImpl`). |
| `CPVRTModelPOD::CopyFromMemory(SPODScene const&)` | `0x3C06D8` | Deep copy of a scene; **richest source of struct sizes**. |
| `CPVRTModelPOD::InitImpl` | `0x3C065C` | Allocates the world-matrix cache (`this[21]`). |
| `CPVRTModelPOD::SavePOD` | `0x3C17EC` | Writer; enumerates every tag on the write side. |

## Table of contents

| File | Contents |
|---|---|
| [`01_container_and_chunk_tags.md`](01_container_and_chunk_tags.md) | Complete chunk-tag table + TLD / `\|0x80000000` container semantics. |
| [`02_scene_and_node_hierarchy.md`](02_scene_and_node_hierarchy.md) | `SPODScene` header, `SPODNode` layout, parenting, ordering. |
| [`03_mesh_and_vertex_data.md`](03_mesh_and_vertex_data.md) | `SPODMesh`, `CPODData`, `EPVRTDataType`, streams, index buffers. |
| [`04_bones_and_skinning.md`](04_bones_and_skinning.md) | **(Deepest)** bone batches, `CreateSkinIdxWeight`, `GetBoneWorldMatrix`. |
| [`05_animation_system.md`](05_animation_system.md) | Node animation channels, flags, `SetFrame`, matrix decomposition, cache. |
| [`06_cameras_lights_materials_textures.md`](06_cameras_lights_materials_textures.md) | `SPODCamera/Light/Material/Texture` + their tags. |
| [`07_read_write_lifecycle.md`](07_read_write_lifecycle.md) | Byte-level read/`SavePOD` write order, ownership, `Destroy`. |
| [`08_arm32_vs_arm64_layout_notes.md`](08_arm32_vs_arm64_layout_notes.md) | Pointer-width / size differences between the two decompilations. |
| [`09_indigenous_gap_analysis.md`](09_indigenous_gap_analysis.md) | Reference → `src/tools/` ✅/⚠️/❌ matrix + remediation backlog. |
| [`10_open_questions_and_unverified.md`](10_open_questions_and_unverified.md) | Every ⚠️ UNVERIFIED item and how to resolve it. |
