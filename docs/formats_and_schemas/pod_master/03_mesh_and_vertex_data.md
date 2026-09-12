# 03 — Mesh, Vertex Streams, CPODData & Index Encoding

> **Derived from:** `sub_3BF6A0` mesh block (`case 0x7DC`), `PVRTFixInterleavedEndianness` (0x3BF544 — reveals SPODMesh DWORD layout), `sub_3C45A8` (`CPODData` reader), `PVRTModelPODDataTypeSize` (0x3BF596), `PVRTModelPODDataTypeComponentCount` (0x3C5088), `PVRTModelPODDataStride`, `CPODData::Reset` (0x3BF2DC).

## 1. SPODMesh — 244 bytes

Mesh stride is **244 bytes** (`SafeAlloc<SPODMesh>` uses `244 * v11`; `PVRTModelPODCopyMesh(..., +244)`). Byte offsets from the mesh block set-up in `sub_3BF6A0` (`v51 = base + 244*i`) and confirmed by `PVRTFixInterleavedEndianness`, which walks the embedded `CPODData` streams by DWORD index:

| Byte | DWORD | Field | Type | Tag | Evidence |
|---|---|---|---|---|---|
| 0 | 0 | nNumVertex | uint32 | 6000 | `v135 = v51` |
| 4 | 1 | nNumFaces | uint32 | 6001 | `v129 = v51+4` |
| 8 | 2 | nNumUVW | uint32 | 6002 | `v141 = v51+8`; loop bound in FixEndianness (`a1[2]`) |
| 12 | 3 | nNumStrips? / unpack flag | uint32 | 6003/6005 | `v105 = v51+12` |
| 16 | 4..? | sFaces (`CPODData`, 16B) | CPODData | 6003 | `v126 = v51+28` region; index stream |
| 28 | 7 | (sFaces.pData / strip data) | ptr | 6004 | `v126 = v51+28` `ReadAfterAlloc32` |
| 36 | 9 | **sVertex** (`CPODData`) | CPODData | 6006 | FixEndianness `(a1+9)` |
| 52 | 13 | **sNormals** (`CPODData`) | CPODData | 6007 | FixEndianness `(a1+13)` |
| 68 | 17 | **sTangents** (`CPODData`) | CPODData | 6008 | FixEndianness `(a1+17)` |
| 84 | 21 | **sBinormals** (`CPODData`) | CPODData | 6009 | FixEndianness `(a1+21)` |
| 100 | 25 | **psUVW** (CPODData* array) | CPODData* | 6002/6010 | `v138 = v51+100`; FixEndianness `a1[25]+16*i` |
| 104 | 26 | **sVtxColours** (`CPODData`) | CPODData | 6011 | FixEndianness `(a1+26)` |
| 120 | 30 | **sBoneIdx** (`CPODData`) | CPODData | 6012 | FixEndianness `(a1+30)` |
| 136 | 34 | **sBoneWeight** (`CPODData`) | CPODData | 6013 | FixEndianness `(a1+34)` |
| 152 | 38 | **pInterleaved** | uint8* | 6014 | `a1[38]` (base for all interleaved streams) |
| 156 | 39 | sBoneBatches.pnBatches | int32* | 6015 | `v120 = v51+156` |
| 160 | 40 | sBoneBatches.pnBatchBoneCnt | int32* | 6016 | `v117 = v51+160` |
| 164 | 41 | sBoneBatches.pnBatchOffset | int32* | 6017 | `v114 = v51+164` |
| 168 | 42 | sBoneBatches.nBatchBoneMax | int32 | 6018 | `v111 = v51+168` |
| 172 | 43 | sBoneBatches.nBatchCnt | int32 | 6019 | `v108 = v51+172` |
| 180 | 45..60 | UnpackMatrix | float[16] | 6020 | `v132 = v51+180` `ReadArray32(16)` |

> Offsets 16–35 (the `sFaces` CPODData and adjacent count fields) are partially inferred: the code sets `v105=v51+12`, `v126=v51+28`. The exact byte split of `sFaces` (a 16-byte `CPODData`) inside 12–35 is **⚠️ UNVERIFIED** — see `10_...md`. The interleaved-stream offsets (9,13,17,21,25,26,30,34,38) are directly proven by `PVRTFixInterleavedEndianness`.

At the mesh close tag (`0x800007DC`) the reader asserts `v53 == *v141` (UVW channels read == nNumUVW) then calls `PVRTFixInterleavedEndianness`.

## 2. CPODData — 16 bytes (the stream descriptor)

Confirmed by `sub_3C45A8` (`v5=a1+2`, `v16=a1+1`, `v15=a1+3`) and `CPODData::Reset`:

| Byte | DWORD | Field | Meaning | Tag |
|---|---|---|---|---|
| 0 | 0 | `eType` | `EPVRTDataType` (see §3) | 9000 |
| 4 | 1 | `n` | components per element | 9001 |
| 8 | 2 | `nStride` | bytes between elements | 9002 |
| 12 | 3 | `pData` | pointer to raw bytes | 9003 |

`CPODData::Reset` defaults: `eType=1`, `n=0`, `nStride=0`, `pData=0` (`*this=1; this[1]=0; this[2]=0; free(this[3]); this[3]=0`).

`PVRTModelPODDataStride(CPODData) = PVRTModelPODDataTypeSize(eType) * n` (0x3C2750). When `pData` is loaded, tag 9003 sizes the read from `PVRTModelPODDataTypeSize(eType)`:
```c
v11 = PVRTModelPODDataTypeSize(*a1);
switch (v11) { case 4: ReadAfterAlloc32<uint>; case 2: ReadAfterAlloc16<ushort>; case 1: ReadAfterAlloc<uchar>; }
```
*(sub_3C45A8, case 0x232B.)* → the reader dispatches on element size 4/2/1.

## 3. EPVRTDataType enum

`PVRTModelPODDataTypeSize(a1)` and `...ComponentCount(a1)` both index tables by `(a1 - 1)`, valid for `a1-1 ≤ 0xD` → **enum values 1..14**:
```c
v1 = a1 - 1; if ( v1 <= 0xD ) return dword_2879CC[v1]; else return 0;   // size
v1 = a1 - 1; if ( v1 <= 0xD ) return dword_287A6C[v1]; else return 0;   // component count
```
So there are **14 data types (1..14)**. The standard PowerVR `EPVRTDataType` in this order is:

| Value | Name | Byte size | Notes |
|---|---|---|---|
| 1 | FLOAT | 4 | default (CPODData::Reset sets 1) |
| 2 | INT | 4 | |
| 3 | UNSIGNED_SHORT | 2 | |
| 4 | RGBA | 4 | packed colour |
| 5 | ARGB | 4 | packed colour |
| 6 | D3DCOLOR | 4 | |
| 7 | UBYTE4 | 4 | 4×byte (bone idx) |
| 8 | DEC3N | 4 | packed normal |
| 9 | FIXED16_16 | 4 | 16.16 fixed point |
| 10 | UNSIGNED_BYTE | 1 | |
| 11 | SHORT | 2 | |
| 12 | SHORT_NORM | 2 | |
| 13 | BYTE | 1 | |
| 14 | BYTE_NORM | 1 | |

> **⚠️ UNVERIFIED at the per-index byte level:** the literal contents of `dword_2879CC` / `dword_287A6C` are `.rodata` arrays IDA did not expand into initialisers in this dump. The **size dispatch actually observed** in `sub_3C45A8` proves only that valid sizes are ∈ {1,2,4}. The type *names/order* above follow the canonical PowerVR SDK `EPVRTDataType` and match the {1,2,4}-size dispatch, but the exact name↔value binding is not independently proven from this binary. Resolution: dump the two arrays from the `.so` at the addresses backing `dword_2879CC`/`dword_287A6C`, or read `PVRTVertexRead`/`PVRTVertexWrite`. Tracked in `10_...md`.

## 4. Interleaved vs. separate streams

- If **`pInterleaved` (mesh+152) is non-null**, all vertex attributes are interleaved into one buffer; each `CPODData.pData` is then a *byte offset* into `pInterleaved` and `CPODData.nStride` is the interleaved stride. `PVRTFixInterleavedEndianness` byte-swaps every stream in place using each `CPODData` descriptor when the host is big-endian (`PVRTIsLittleEndian` == 0). On this build `PVRTIsLittleEndian` always returns 1, so no swap occurs.
- If `pInterleaved` is null, each stream owns its own `pData` allocation (separate arrays).

`PVRTModelPODDataConvert` (0x3C2764) re-packs a stream to a new `EPVRTDataType` using `PVRTVertexRead`/`PVRTVertexWrite`, recomputing `n` (via the component-count table, mask `0x7E0E` selects vector types) and `nStride = size*n`.

## 5. Index buffer (`sFaces`, tag 6003)

- Faces are stored as a `CPODData` block (tag 6003 → `sub_3C45A8`). Index width follows `sFaces.eType`: **UNSIGNED_SHORT (2 bytes)** or **UNSIGNED_INT/INT (4 bytes)**, dispatched by `PVRTModelPODDataTypeSize` == 2 vs 4 in the 9003 reader.
- Triangle **strips**: tag 6005 (`NumStrips`) > 0 means the index list is a set of strips whose lengths are in tag 6004 (`ReadAfterAlloc32` array). Otherwise indices are a flat triangle list.
- Our loader widens all indices to `uint32_t` (`pod_loader.h` `indices` comment "widened to u32"). That is a lossless superset — fine for consumption, but a round-trip writer must re-narrow to the original `eType` to be byte-exact (see `09_...md`).

## 6. Fixed-point toggle

`PVRTModelPODToggleFixedPoint(scene)` is invoked at scene close when scene byte 80 low bit is set. It converts fixed-point (`FIXED16_16`, type 9) streams to/from float across the whole model. Stock Swordigo assets are float (`pod_loader.h` notes UnpackMatrix is identity in all stock PODs), so this path is rarely exercised but must be honoured for external assets.
