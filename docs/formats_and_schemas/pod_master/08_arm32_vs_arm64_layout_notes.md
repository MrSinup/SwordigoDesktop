# 08 — arm32 vs arm64 Layout Differences

> **Derived from:** `arm32_13/functions/CPVRTModelPOD/00000000003C06D8__CopyFromMemory.c` vs `arm64_13/functions/CPVRTModelPOD/0000000000583784__CopyFromMemory.c`; `sub_3BF6A0` (both trees); `GetWorldMatrixNoCache` (both).

**Rule of thumb:** the *file format is identical* across architectures (same tags, same on-disk field order and byte widths). Only the **in-memory struct sizes and offsets** differ, because every pointer is 4 bytes on arm32 and 8 bytes on arm64, and structs re-pad accordingly. All the byte offsets in docs `02`–`06` are the **arm32** layout unless noted; use this file to translate to arm64.

## 1. Struct sizes (VERIFIED from both CopyFromMemory copies)

| Struct | arm32 stride | arm64 stride | Evidence |
|---|---|---|---|
| SPODNode | **52** | **96** | arm32 `v9 += 52`; arm64 `v9 += 96` |
| SPODMesh | **244** | **344** | arm32 `v13 += 244`; arm64 `v13 += 344` |
| SPODCamera | **20** | **24** | arm32 `+= 20`; arm64 `v17 += 24` |
| SPODLight | **40** | **40** | arm32 `+= 40`; arm64 `v28 += 40` (no pointer fields beyond target → same) |
| SPODTexture | **4** | **8** | arm32 single `char*` (4); arm64 pointer (8) |
| SPODMaterial | **156** | **176** | arm32 `+= 156`; arm64 `v48 += 176` |

SPODLight is the same size on both because its only pointer is implicit (colour is inline floats, target is an int); its 40 bytes are all 32-bit scalars.

## 2. Scene-header offsets differ (pointer fields)

The scene header interleaves `uint32 count` + `pointer`. On arm64 the pointers widen and shift every subsequent field. Confirmed offsets:

| Field | arm32 byte | arm64 byte | Evidence |
|---|---|---|---|
| pNode | 56 | 80 | arm32 `a1+56`; arm64 `*(a1+80)` |
| pMesh | 44 | 64 | arm32 `a2[11]`(=44); arm64 `*(a2+64)` |
| pMaterial | 72 | 112 | arm32 `a2[18]`(=72); arm64 `*(a1+112)` |
| nNumFrame source | 76 | 120 | arm32 `*(a2+19)`; arm64 `*(unsigned int*)(a2+120)` |

## 3. Node field offsets differ

The arm32 node packs 13 DWORDs (indices + 8 pointers) into 52 bytes. On arm64 the 8 pointers become 8 bytes each, so:
- arm32 `pfAnimMatrix` at **+48**; arm64 the animated-matrix pointer is at **+48** as an 8-byte read (`v15 = *(_QWORD *)(a3 + 48)`, arm64 `GetWorldMatrixNoCache`), but the *flags/kind* byte the arm64 path tests is at **+24** (`*(_BYTE*)(a3+24) & 8`, `& 2`), whereas arm32 selects matrix-vs-TRS purely by `pfAnimMatrix != 0` at +48. The arm64 build carries an explicit flag byte; arm32 infers it.
- Therefore, **trust arm64 for pointer widths and for the presence of the flag byte; trust arm32 for the compact offset reasoning and readability.** For any consumer we write, key on the *tag-driven* fields (from docs 01–06), not raw offsets, so architecture is irrelevant.

## 4. Which decompilation was trusted for what

| Concern | Trusted source | Why |
|---|---|---|
| Tag values & block nesting | arm32 `sub_3BF6A0` | cleanest switch, matches arm64 tag-for-tag |
| Struct *sizes* | both (agree after pointer widening) | `CopyFromMemory` in each tree |
| Pointer field widths | arm64 | 8-byte reads make widths explicit |
| CPODData 16-byte layout | arm32 `sub_3C45A8` | arm64 widens `pData` to 8 → 20/24 bytes there |
| Skinning / matrix algorithms | arm32 `CreateSkinIdxWeight`, `GetBoneWorldMatrix` | identical logic, arm32 more readable |
| Endianness handling | arm32 | sentinel + `PVRTFixInterleavedEndianness` legible |

## 5. Consequence for our tooling

`src/tools/pod_loader.cpp` reads the **file**, not a struct dump, so it is architecture-independent by construction — good. The only place arch matters is if we ever `qmemcpy` a raw `SPOD*` buffer between a 32-bit dump and our 64-bit process; we never do. No action required beyond keeping doc offsets labelled arm32.

> **⚠️ UNVERIFIED:** exact arm64 byte offsets for every field within the widened Node/Mesh/Material structs are not enumerated here (only sizes + the few offsets the world-matrix path touches). If a raw-struct interop path is ever added, dump `arm64_13` `CopyFromMemory` in full to recover them. Tracked in `10_...md`.
