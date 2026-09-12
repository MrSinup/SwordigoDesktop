# 09 — Indigenous Gap Analysis (`src/tools/` vs reference)

> **Derived from:** reference `sub_3BF6A0`, `sub_3C45A8`, `CreateSkinIdxWeight`, `GetBoneWorldMatrix`, `SetFrame`, `SavePOD` (see docs 01–07) compared to `src/tools/pod_loader.{h,cpp}`, `pod_writer.{h,cpp}`, `pod_convert.{h,cpp}`.

Legend: ✅ Implemented (matches) · ⚠️ Implemented but differs · ❌ Missing.

## 1. Framing & scene

| Feature | Reference | src/tools | Status | Citations |
|---|---|---|---|---|
| TLD 8-byte marker | `ReadMarker(tag,len)` | `chunk()`/`begin()`/`u32` | ✅ | `pod_writer.cpp:106-118` |
| Container close `\|0x80000000` | `sub_3C45A8 a3\|0x80000000` | `kEndTagMask=0x80000000` | ✅ | `pod_loader.cpp:24`, `pod_writer.cpp:17` |
| Version `"AB.POD.2.0"` (1000) | strcmp check | `eFormatVersion=1000` handled | ✅ | `pod_loader.cpp:27,779` |
| Scene open (1001) | `case 0x3E9` | `eScene=1001` | ✅ | `pod_loader.cpp:28,784` |
| Endianness sentinel / big-endian reject | present (`-402456576`) | none | ❌ | reader assumes LE; add a guard |
| `PVRTModelPODToggleFixedPoint` (scene byte 80 bit) | present | none | ❌ | fixed-point PODs mis-read (none in stock assets) |
| Scene-close count validation | asserts every `Num*` | not enforced | ⚠️ | `pod_loader.cpp` reads counts but does not hard-fail on mismatch |
| **FPS tag** | **2016 (0x7E0)** | **`eSceneFPS = 2017`** | ⚠️ **MISMATCH** | ref `sub_3BF6A0 case 0x7E0`; `pod_loader.cpp:37,744` |

> **FPS finding:** the reference read path stores FPS from tag **2016 (0x7E0)** into scene byte 80, and treats **2017** as unused. Our loader reads FPS from **2017**. Either the assets we consume actually emit 2017 (a PowerVR SDK variant) or we are silently defaulting FPS. **Action:** dump a stock `.pod` and check whether 0x7E0 (2016) or 0x7E1 (2017) carries FPS; align the constant. This is the single most concrete correctness risk surfaced.

## 2. Nodes & animation

| Feature | Reference tag | src/tools | Status |
|---|---|---|---|
| Node idx/name/material/parent (5000-5003) | ✓ | `eNodeIndex..eNodeParentIndex` | ✅ (`pod_loader.cpp:53-56`) |
| Default TRS (5004-5006) | ✓ | `eNodePosition/Rotation/Scale` | ✅ (`:57-59`) |
| Anim TRS arrays (5007-5009) | ✓ | `eNodeAnimation*` | ✅ (`:60-62`) |
| Matrix + AnimMatrix (5010/5011) | ✓ | `eNodeMatrix/AnimationMatrix` | ✅ (`:63-64`) |
| AnimFlags (5012) | ✓ bits 1/2/4 | `eNodeAnimationFlags` + `anim_flags` | ✅ (`:65`, `pod_loader.h`) |
| Anim index arrays (5013-5016) | ✓ sparse | `eNodeAnimation*Index` | ✅ (`:66-69`) |
| **Scale channel = 7 floats/key** | 0x1C malloc | `anim_scale; // 7 * num_frames` | ✅ (matches, `pod_loader.h`) |
| `SetFrame` blend (nFrame/fBlend) | ✓ | `get_node_matrix(frame)` fractional | ✅ (`pod_loader.h`) |
| World-matrix cache (zero+anim slots) | ✓ | computed on demand (no cache) | ⚠️ correctness OK, perf differs — acceptable |
| Quaternion slerp vs lerp | ⚠️ unverified in ref | our impl choice | ⚠️ verify against ref once slerp math resolved |

## 3. Mesh, streams & indices

| Feature | Reference | src/tools | Status |
|---|---|---|---|
| NumVerts/NumFaces (6000/6001) | ✓ | `eMeshNumVertices/NumFaces` | ✅ (`:75-76`) |
| UVW channels (6002/6010) | `psUVW[]` array, count-checked | `eMeshNumUVWChannels/UVWList` | ✅ (`:77,85`) |
| Index list (6003) | `CPODData`, width by eType | `eMeshVertexIndexList`, widened to u32 | ⚠️ read OK; writer must re-narrow (see §5) |
| Strips (6004/6005) | length array + NumStrips | `eMeshStripLengthList/NumStrips` | ✅ (`:79-80`) |
| Vertex/Normal/Tangent/Binormal (6006-6009) | ✓ | `eMeshVertexList..BinormalList` | ✅ (`:81-84`) |
| Vertex colours (6011) | ✓ | `eMeshVertexColourList` | ✅ (`:86`) |
| Bone idx/weight (6012/6013) | `CPODData` | `eMeshBoneIndexList/WeightList` | ✅ (`:87-88`) |
| Interleaved data (6014) | `pInterleaved` + offset streams | `eMeshInteravedDataList` | ⚠️ present as constant (`:89`); verify de-interleave path |
| Bone batches (6015-6019) | ✓ | `eMeshBoneBatch*` + `BoneBatch` struct | ✅ (`:90-94`, `pod_loader.h`) |
| UnpackMatrix (6020) | `ReadArray32(16)` | `eMeshUnpackMatrix` + `unpack_matrix` | ✅ (`:95`) |
| MeshType (6021) | **not in ref read path** | `eMeshType` | ⚠️ our extension — harmless, but not round-trippable to stock reader |
| Adjacency (6022) | **not in ref read path** | `eMeshAdjacencyIndexList` | ⚠️ our extension |
| **CPODData eType/n/stride/data (9000-9003)** | ✓ (16-byte descriptor) | `eBlockDataType..eBlockData` | ✅ (`:100-103`) |
| **EPVRTDataType full enum (14 types)** | table-driven 1..14 | only float/int/short/ubyte handled | ⚠️ non-float types (DEC3N/FIXED16_16/RGBA/normalised) not de/encoded |

## 4. Cameras, lights, materials, textures

| Feature | Reference | src/tools | Status |
|---|---|---|---|
| **Camera block (2010 / 8000-8004)** | full | **none** | ❌ MISSING |
| **Light block (2011 / 7000-7007)** | full | **none** (only shader lighting) | ❌ MISSING |
| Material name/diffuseTex/opacity/diffuse (3000/3001/3002/3004) | ✓ | `eMaterial*` | ✅ (`:44-47`) |
| Material ambient/specular/shininess (3003/3005/3006) | ✓ | none | ❌ MISSING |
| Material effect file/name (3007/3008) | ✓ | none | ❌ MISSING |
| Material blend state (3009-3026) | ✓ | none | ❌ MISSING |
| Texture filename (4000) | ✓ | `eTextureFilename` | ✅ (`:50,648`) |

## 5. Writer (`pod_writer.cpp`) round-trip fidelity

| Concern | Reference `SavePOD` | `pod_writer.cpp` | Status |
|---|---|---|---|
| Emits version + scene container | ✓ | ✓ | ✅ (`:260`) |
| Writes CPODData sub-blocks (9000-9003) | ✓ | `write_vertex_block` | ✅ (`:82-85,131`) |
| Index width preserved (u16 vs u32) | by original eType | `write_index_block(uint32_t)` — always u32 | ⚠️ widens; not byte-exact vs a u16-index stock POD |
| Bone weight renormalise-to-255 | `CreateSkinIdxWeight` | float weights written raw | ❌ if targeting the GPU byte path; OK for float streams |
| Cameras / lights written | ✓ | not written | ❌ (mirrors loader gap) |
| Full material state written | ✓ | name/tex/opacity/diffuse only | ⚠️ lossy |
| Interleaved output | optional | writes separate streams | ⚠️ valid (separate streams are legal) but not byte-identical to interleaved source |

## 6. Skinning (`skin_mesh`, `pod_convert.cpp`)

| Feature | Reference | src/tools | Status |
|---|---|---|---|
| 4 bones/vertex | `CreateSkinIdxWeight` | `bones_per_vertex` (variable) | ⚠️ we allow non-4; stock reader assumes 4 |
| 255-bone limit | error at ≥256 | not enforced | ⚠️ add validation |
| Byte weights summing to 255 | ✓ (redistribute) | float weights | ⚠️ for byte export must quantise+renormalise |
| Local-batch idx → global node | `pnBatches[base+local]` | present in `BoneBatch` | ✅ (verify resolution in `skin_mesh`) |
| Bind-pose-relative matrix | `World_bind·World_bind(bone)⁻¹·World_cur(bone)` | `bind_matrix` captured separately | ✅ (deliberate: our anim-merge uses true export pose, not frame 0 — documented in `04_...md`) |

## 7. Prioritised remediation backlog

**P0 — correctness**
1. **Resolve the FPS tag (2016 vs 2017).** Dump a stock `.pod`; align `eSceneFPS`. (`pod_loader.cpp:37`)
2. **Add camera (2010) and light (2011) parsing** — currently entirely absent; any POD with these silently drops them and the scene-close count check (if added) would fail.
3. **Enforce scene-close `Num*` validation** to catch truncated/misaligned reads early.

**P1 — bones / animation / skinning (mission-critical depth)**
4. On skin **export**, quantise weights to bytes and **renormalise to sum 255** exactly as `CreateSkinIdxWeight`; enforce the **255-bone** cap.
5. Verify `skin_mesh` resolves per-vertex indices as **local-to-batch** (`pnBatches[batchBase+local]`), not global node indices.
6. Confirm quaternion interpolation matches the reference (slerp vs normalised-lerp) once §10 resolves the math.

**P2 — fidelity / round-trip**
7. Preserve original **index width** (u16/u32) on write instead of always widening to u32.
8. Support the full **EPVRTDataType** set (DEC3N, FIXED16_16, RGBA/ARGB, normalised byte/short) in both read and write.
9. Write **full material state** (ambient/specular/shininess/effect/blend) and honour `PVRTModelPODToggleFixedPoint`.

**P3 — extensions**
10. Our `MeshType (6021)` / `Adjacency (6022)` are not in the stock reader's path — keep them but gate behind a flag so byte-exact stock round-trips remain possible.
