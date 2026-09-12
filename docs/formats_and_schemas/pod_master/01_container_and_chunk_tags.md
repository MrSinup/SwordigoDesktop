# 01 — Container Framing & Complete Chunk-Tag Table

> **Derived from:** `arm32_13/libswordigo_ida.c` `sub_3BF6A0` (the master read loop, target of `CPVRTModelPOD::ReadFromMemory`/`ReadFromFile`); `sub_3C45A8` (`CPODData` reader); cross-checked against `src/tools/pod_loader.cpp` enum constants (lines 27–103).

## 0. Empirical validation (real assets)

This tag table was verified against **the full stock asset set — 423 `.pod` files** in `~/.local/share/swordigo-desktop/assets/resources/` using `src/tools/pod_dump.py`:

- **422 / 423 fully parsed** with **0 unknown tags** and **100% of bytes consumed** (the one exception, `dummy.pod`, is an 8-byte empty placeholder).
- Confirmed on base meshes (`hiro.POD`: 5 meshes, 61 nodes, 5 materials/textures, interleaved skinned vertex data), animation-only PODs (`hiro_hurt.POD`: 0 meshes, 61 nodes, 12 frames, per-node AnimPosition/Rotation/Scale), and environment props.
- **EPVRTDataType values seen in real data:** `1=FLOAT`, `2=INT` (bone index), `3=UNSIGNED_SHORT` (indices), `5=ARGB` (vertex colour) — empirically confirming those enum bindings (see `03_...md` §3 / `10_...md` #1).

### Framing refinement discovered from real files
Every chunk — **leaf as well as container** — is bracketed by a matching close marker `open | 0x80000000` (with length 0), i.e.:

```
[open tag][len][ payload (leaf) OR nested markers (container) ][open|0x80000000][0]
```

So leaves are `open · payload · close`, not a bare header. The `close = open | 0x80000000` rule (below) therefore applies to *all* tags, which is exactly why `pod_dump.py` recurses and consumes a trailing close marker after every leaf. (The decompiled reader tolerates this because it reads by marker, treating the length as the payload span.)

## 1. Tag-Length-Data (TLD) framing

Every chunk is read through `CSource::ReadMarker(src, &tag, &length)`:

```c
while ( CSource::ReadMarker(a2, &v172, &v171) )   // v172 = tag, v171 = length
    switch ( v172 ) { ... }
```
*(arm32_13 `sub_3BF6A0`.)* Each marker is an 8-byte header: `uint32 tag`, `uint32 length`. The baseline spec's framing diagram (`pod_3d_model_format_spec.md` §"Every POD file consists of…") is confirmed.

## 2. Container open/close rule — VERIFIED

An **open/container** tag is compared as-is; its matching **close** tag has the top bit set: `close = open | 0x80000000`. The decompiler renders the close comparisons as the signed negatives below. Worked confirmations from `sub_3BF6A0`:

| Block open | close constant in code | `open \| 0x80000000` | ✔ |
|---|---|---|---|
| Camera `0x7DA` (2010) | `-2147481638` | `0x800007DA` = 2147485658 → signed `-2147481638` | ✔ |
| Light `0x7DB` (2011) | `-2147481637` | `0x800007DB` | ✔ |
| Mesh `0x7DC` (2012) | `-2147481636` | `0x800007DC` | ✔ |
| Node `0x7DD` (2013) | `-2147481635` | `0x800007DD` | ✔ |
| Texture `0x7DE` (2014) | `-2147481634` | `0x800007DE` | ✔ |
| Material `0x7DF` (2015) | `-2147481633` | `0x800007DF` | ✔ |
| Scene `0x3E9` (1001) | `-2147482647` | `0x800003E9` | ✔ |

`sub_3C45A8` computes the close explicitly: `v6 = a3 | 0x80000000; ... if ( v21 == v6 ) return 1;` — decisive proof of the rule.

Unknown tags are skipped by seeking past `length` bytes via the virtual `Skip` at vtbl+12: `(*(...)(*(_DWORD*)a2 + 12))(a2, length)`.

## 3. Complete chunk-tag table

Legend: **C** = container (opens nested markers), **L** = leaf (payload read directly). Payload type from the `CSource::Read*` call used.

### 3.1 File / scene framing

| Dec | Hex | Name | C/L | Parent | Payload | Evidence |
|---|---|---|---|---|---|---|
| 1000 | 0x3E8 | FormatVersion | L | (root) | char[11] `"AB.POD.2.0"` | `case 0x3E8u`, `strcmp("AB.POD.2.0")` |
| 1001 | 0x3E9 | Scene | C | (root) | — opens scene | `case 0x3E9u` |
| 1002 | 0x3EA | (export/history data 1) | L | root | skipped/opaque | `case 0x3EAu` |
| 1003 | 0x3EB | (export/history data 2) | L | root | skipped/opaque | `case 0x3EBu` |

### 3.2 Scene fields (children of 0x3E9)

| Dec | Hex | Name | C/L | Payload | Evidence (`sub_3BF6A0` inner switch on `v174`) |
|---|---|---|---|---|---|
| 2000 | 0x7D0 | Colour background | L | float[3] `ReadArray32(...,3)` | `case 0x7D0u` → `ReadArray32(v9, 3)` |
| 2001 | 0x7D1 | Colour ambient | L | float[3] | `case 0x7D1u` → `v9+3, ReadArray32(3)` |
| 2002 | 0x7D2 | NumCamera | L | uint32 → `SafeAlloc<SPODCamera>` | `case 0x7D2u` |
| 2003 | 0x7D3 | NumLight | L | uint32 → `SafeAlloc<SPODLight>` | `case 0x7D3u` |
| 2004 | 0x7D4 | NumMesh | L | uint32 → `SafeAlloc<SPODMesh>` | `case 0x7D4u` |
| 2005 | 0x7D5 | NumNode | L | uint32 → `SafeAlloc<SPODNode>` | `case 0x7D5u` |
| 2006 | 0x7D6 | NumMeshNode | L | uint32 (`v9+13`) | `case 0x7D6u` |
| 2007 | 0x7D7 | NumTexture | L | uint32 → `SafeAlloc<SPODTexture>` | `case 0x7D7u` |
| 2008 | 0x7D8 | NumMaterial | L | uint32 → `SafeAlloc<SPODMaterial>` | `case 0x7D8u` |
| 2009 | 0x7D9 | NumFrame | L | uint32 (`v9+19`) | `case 0x7D9u` |
| 2010 | 0x7DA | Camera | C | opens camera block | `case 0x7DAu` |
| 2011 | 0x7DB | Light | C | opens light block | `case 0x7DBu` |
| 2012 | 0x7DC | Mesh | C | opens mesh block | `case 0x7DCu` |
| 2013 | 0x7DD | Node | C | opens node block | `case 0x7DDu` |
| 2014 | 0x7DE | Texture | C | opens texture block | `case 0x7DEu` |
| 2015 | 0x7DF | Material | C | opens material block | `case 0x7DFu` |
| 2016 | 0x7E0 | FPS | L | uint32 (`v9+20`) | `case 0x7E0u` → `Read32(v9+20)` |

> **Note:** `2016 (0x7E0)` is read into scene DWORD offset 20 (byte 80). Our loader labels **2017** as FPS (`eSceneFPS = 2017`, `pod_loader.cpp:37`). The reference read path reads FPS at **2016**. See `09_indigenous_gap_analysis.md` — this is a real mismatch.

### 3.3 Camera block (children of 0x7DA)

| Dec | Hex | Name | Payload | Evidence |
|---|---|---|---|---|
| 8000 | 0x1F40 | Target-object index | int32 | `case 8000` `Read32<int>(v145)` |
| 8001 | 0x1F41 | Field of view | float | `case 8001` |
| 8002 | 0x1F42 | Far clip | float | `case 8002` |
| 8003 | 0x1F43 | Near clip | float | `case 8003` |
| 8004 | 0x1F44 | FOV animation array | float[] `ReadAfterAlloc32` | `case 8004` |

### 3.4 Light block (children of 0x7DB)

| Dec | Hex | Name | Payload | Evidence |
|---|---|---|---|---|
| 7000 | 0x1B58 | Target-object index | int32 | `case 7000` |
| 7001 | 0x1B59 | Colour | float[3] `ReadArray32(3)` | `case 7001` |
| 7002 | 0x1B5A | Light type (`EPODLight`) | int32 enum | `case 7002` `Read32<EPODLight>` |
| 7003 | 0x1B5B | Constant attenuation | float | `case 7003` |
| 7004 | 0x1B5C | Linear attenuation | float | `case 7004` |
| 7005 | 0x1B5D | Quadratic attenuation | float | `case 7005` |
| 7006 | 0x1B5E | Falloff angle | float | `case 7006` |
| 7007 | 0x1B5F | Falloff exponent | float | `case 7007` |

### 3.5 Mesh block (children of 0x7DC)

| Dec | Hex | Name | Payload | Evidence |
|---|---|---|---|---|
| 6000 | 0x1770 | NumVertices | uint32 | `case 6000` |
| 6001 | 0x1771 | NumFaces | uint32 | `case 6001` (`v129 = v51+4`) |
| 6002 | 0x1772 | NumUVWChannels | uint32 → `SafeAlloc<CPODData>` | `case 6002` |
| 6003 | 0x1773 | VertexIndexList (`sFaces`) | CPODData block | `case 6003` → `sub_3C45A8` |
| 6004 | 0x1774 | StripLengths | uint32[] `ReadAfterAlloc32` | `case 6004` |
| 6005 | 0x1775 | NumStrips | uint32 | `case 6005` |
| 6006 | 0x1776 | VertexList (`sVertex`) | CPODData block | `case 6006` → `sub_3C45A8(v135)` |
| 6007 | 0x1777 | NormalList (`sNormals`) | CPODData block | `case 6007` |
| 6008 | 0x1778 | TangentList | CPODData block | `case 6008` |
| 6009 | 0x1779 | BinormalList | CPODData block | `case 6009` |
| 6010 | 0x177A | UVWList (per channel) | CPODData block | `case 6010` `sub_3C45A8(*v138 + 16*i)` |
| 6011 | 0x177B | VertexColourList | CPODData block | `case 6011` |
| 6012 | 0x177C | BoneIndexList (`sBoneIdx`) | CPODData block | `case 6012` |
| 6013 | 0x177D | BoneWeightList (`sBoneWeight`) | CPODData block | `case 6013` |
| 6014 | 0x177E | InterleavedData | uint8[] `ReadAfterAlloc` | `case 6014` (`pInterleaved`) |
| 6015 | 0x177F | BoneBatchIndexList | int32[] `ReadAfterAlloc32` | `case 6015` |
| 6016 | 0x1780 | BoneBatchBoneCounts | int32[] | `case 6016` |
| 6017 | 0x1781 | BoneBatchOffsets | int32[] | `case 6017` |
| 6018 | 0x1782 | MaxNumBonesPerBatch | int32 | `case 6018` `Read32<int>(v111)` |
| 6019 | 0x1783 | NumBoneBatches | int32 | `case 6019` `Read32<int>(v108)` |
| 6020 | 0x1784 | UnpackMatrix | float[16] `ReadArray32(16)` | `case 6020` `ReadArray32(v132, 0x10)` |

> The reference mesh switch ends at **6020**. Tags **6021 (MeshType)** and **6022 (Adjacency)** appear in `src/tools/pod_loader.cpp` (lines 96–97) but **not** in this reference read path. Flagged in `09_indigenous_gap_analysis.md`.

### 3.6 Node block (children of 0x7DD)

| Dec | Hex | Name | Payload | Evidence |
|---|---|---|---|---|
| 5000 | 0x1388 | Index (object index) | int32 | `case 0x1388` `Read32(v130)` |
| 5001 | 0x1389 | Name | char[] `ReadAfterAlloc` | `case 0x1389` |
| 5002 | 0x138A | MaterialIndex | int32 | `case 0x138A` |
| 5003 | 0x138B | ParentIndex | int32 | `case 0x138B` |
| 5004 | 0x138C | Position (anim) | float[3] `ReadArray32(3)` | `case 0x138C` (sets `v148=1`) |
| 5005 | 0x138D | Rotation (anim) | float[4] `ReadArray32(4)` | `case 0x138D` (`v73=4`) |
| 5006 | 0x138E | Scale (anim) | float[3] `ReadArray32(3)` | `case 0x138E` (`v73=3`) |
| 5007 | 0x138F | AnimPosition array | float[] `ReadAfterAlloc32` | `case 0x138F` |
| 5008 | 0x1390 | AnimRotation array | float[] | `case 0x1390` |
| 5009 | 0x1391 | AnimScale array | float[] | `case 0x1391` |
| 5010 | 0x1392 | Matrix (default fallback) | consumed via default | `case 0x1392` → `LABEL_197` |
| 5011 | 0x1393 | AnimMatrix array | float[] `ReadAfterAlloc32` | `case 0x1393` |
| 5012 | 0x1394 | AnimFlags | uint32 | `case 0x1394` `Read32(v142)` |
| 5013 | 0x1395 | AnimPositionIdx | uint32[] `ReadAfterAlloc32` | `case 0x1395` |
| 5014 | 0x1396 | AnimRotationIdx | uint32[] | `case 0x1396` |
| 5015 | 0x1397 | AnimScaleIdx | uint32[] | `case 0x1397` |
| 5016 | 0x1398 | AnimMatrixIdx | uint32[] | `case 0x1398` |

See `05_animation_system.md` for the close-tag (`0x800007DD`) fix-up that mallocs default position/rotation/scale and sets `AnimFlags` bits `1/2/4`.

### 3.7 Texture block (children of 0x7DE)

| Dec | Hex | Name | Payload | Evidence |
|---|---|---|---|---|
| 4000 | 0xFA0 | Filename | char[] `ReadAfterAlloc` | `case 4000` |

### 3.8 Material block (children of 0x7DF)

| Dec | Hex | Name | Payload | Evidence |
|---|---|---|---|---|
| 3000 | 0xBB8 | Name | char[] | `case 3000` |
| 3001 | 0xBB9 | DiffuseTextureIndex (idx set, memset 0xFF) | int32 | `case 3001` |
| 3002 | 0xBBA | Opacity | float | `case 3002` `Read32<float>(v119)` |
| 3003 | 0xBBB | Ambient | float[3] `ReadArray32(3)` | `case 3003` |
| 3004 | 0xBBC | Diffuse | float[3] | `case 3004` |
| 3005 | 0xBBD | Specular | float[3] | `case 3005` |
| 3006 | 0xBBE | Shininess | float | `case 3006` |
| 3007 | 0xBBF | Effect file | char[] `ReadAfterAlloc` | `case 3007` |
| 3008 | 0xBC0 | Effect name | char[] | `case 3008` |
| 3009 | 0xBC1 | Blend src RGB | int32 | `case 3009` |
| 3010–3017 | 0xBC2–0xBC9 | Blend dst RGB / src+dst alpha / factors | int32 each | `case 3010..3017` `Read32<int>` |
| 3018 | 0xBCA | Blend op RGB (`EPODBlendFunc`) | int32 enum | `case 3018` `Read32<EPODBlendFunc>` |
| 3019–3021 | 0xBCB–0xBCD | Blend colour / factor components | `EPODBlendFunc` | `case 3019..3021` |
| 3022 | 0xBCE | Blend operation (`EPODBlendOp`) | int32 enum | `case 3022` `Read32<EPODBlendOp>` |
| 3023 | 0xBCF | Blend op alpha | `EPODBlendOp` | `case 3023` |
| 3024 | 0xBD0 | Blend colour | float[4] `ReadArray32(4)` | `case 3024` |
| 3025 | 0xBD1 | Blend factor | float[4] | `case 3025` |
| 3026 | 0xBD2 | Flags | uint32 | `case 3026` `Read32(v125)` |

> Exact semantic split of 3009–3021 into individual blend-state fields is **⚠️ UNVERIFIED** at field granularity (the decompiler only shows the target byte offsets, not PowerVR names). The offsets themselves are cited in `06_...md`. Names above follow PowerVR SDK convention.

### 3.9 CPODData sub-block (children of any 6003/6006–6013 stream)

| Dec | Hex | Name | Payload | Evidence (`sub_3C45A8`) |
|---|---|---|---|---|
| 9000 | 0x2328 | DataType (`EPVRTDataType`) | int32 → `a1[0]` | `case 0x2328u` |
| 9001 | 0x2329 | NumComponents (`n`) | uint32 → `a1[1]` | `case 0x2329u` |
| 9002 | 0x232A | Stride (`nStride`) | uint32 → `a1[2]` | `case 0x232Au` |
| 9003 | 0x232B | Data (`pData`) | raw bytes → `a1[3]` | `case 0x232Bu` (size from `PVRTModelPODDataTypeSize`) |

## 4. Summary vs. baseline spec

The baseline `pod_3d_model_format_spec.md` documented only **9 tags** (1000, 2004, 2009, 2012, 6000, 6001, 6006, 6007, 9003). This table recovers **~90 tags** across 8 block types. The container `|0x80000000` rule is confirmed against three independent code paths.
