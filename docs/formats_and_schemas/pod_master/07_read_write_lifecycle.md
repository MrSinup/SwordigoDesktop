# 07 — Read / Write Lifecycle, Ownership & I/O Plumbing

> **Derived from:** `ReadFromFile` (0x3BF618), `ReadFromMemory` (0x3C0240), `sub_3BF6A0`, `CopyFromMemory` (0x3C06D8), `InitImpl` (0x3C065C), `Destroy` (0x3C02EA), `SavePOD` (0x3C17EC), `CSourceStream`/`CSource` helpers.

## 1. Read entry & I/O plumbing

```
ReadFromFile(path, ...)   → CSourceStream::Init(path)      → sub_3BF6A0(this, src, ...)
ReadFromMemory(buf, len, ...) → CSourceStream::Init(buf,len) → sub_3BF6A0(this, src, ...)
```
Both build a `CSourceStream` (vtbl `off_44DB98`), then call the shared reader and destroy the stream. `CSource` is the abstract reader interface; `CSourceStream` reads from a file/memory buffer. The reader only ever uses these `CSource` virtuals:

| vtbl slot | Method | Use |
|---|---|---|
| +8 | `Read(dst, n)` | bulk read n bytes |
| +12 | `Skip(n)` | seek past unknown chunk payloads |
| — | `ReadMarker(&tag,&len)` | read an 8-byte TLD header |
| — | `Read32<T>`, `ReadArray32`, `ReadAfterAlloc[16/32]<T>` | typed reads (with allocation) |

`ReadAfterAlloc*` first reads a `uint` element count, allocates `count * sizeof(T)`, then reads the payload — used for all variable-length arrays (names, anim arrays, index lists, interleaved data).

## 2. Master read order (`sub_3BF6A0`)

1. `memset(scene, 0, 0x58)` — zero the 88-byte scene header (84 header + impl ptr slot).
2. Loop `ReadMarker` on the **outer** stream:
   - `0x3E8` FormatVersion → must equal `"AB.POD.2.0"`, sets `v10=1`.
   - `0x3E9` Scene → enter **inner** loop (`LABEL_35`).
   - `0x3EA/0x3EB` history/export blocks (skipped for normal loads).
   - endianness sentinel (`-402456576`) → error if flipped.
3. Inner loop reads scene fields `0x7D0–0x7E0`; each `NumX` tag `SafeAlloc`s the corresponding array; each container tag (`0x7DA–0x7DF`) parses one element into the pre-allocated array slot (`v149..v154` running counters).
4. Scene close (`0x800003E9`) validates every counter equals its declared `Num*`; sets `v16=1`.
5. If not "header-only" (`v167==0`): if scene byte 80 low bit set → `PVRTModelPODToggleFixedPoint`; require `v10 & v16` (version seen AND scene closed) else fail.
6. `InitImpl(scene)` → allocate world-matrix cache. Return 0 (success).

## 3. `InitImpl` & impl allocation

`InitImpl` (0x3C065C): `delete this[21]; this[21] = new CPVRTModelPODImpl(0x1C=28 bytes)`; zeroes it; then:
- `pfCache = new float[nNumNode]`  (`4 * nNumNode`, clamped)
- `pWmZeroCache = new byte[nNumNode << 6]` (64 bytes/matrix)
- `pWmCache     = new byte[nNumNode << 6]`
- `FlushCache()`.

`ReadFromMemory(SPODScene const&)` additionally sets impl byte 24 = 1 (marks "external scene, do not free source arrays").

## 4. Ownership & `Destroy`

`Destroy` (0x3C02EA) frees, in order: node names & anim arrays, mesh `CPODData.pData`/interleaved/bone-batch arrays (`CPVRTBoneBatches::Release`), camera anim FOV arrays, light arrays, texture names, material name/effect strings, then the top-level `pNode/pMesh/pCamera/pLight/pTexture/pMaterial` arrays and the impl cache. `CopyFromMemory` calls `Destroy` first, so re-reading into a live model is safe. The whole model owns all its heap data unless it was adopted via `ReadFromMemory(SPODScene const&)`.

## 5. `CopyFromMemory` (deep copy)

Copies the 84-byte header, then for each block `SafeAlloc`s `count * stride` and calls the per-type deep-copy helper:
`PVRTModelPODCopyNode` (+52), `...Mesh` (+244), `...Camera` (+20), `...Light` (+40), `...Texture` (+4), `...Material` (+156). Finishes with `InitImpl`. This function is the authoritative source for the six struct strides used throughout these docs.

## 6. Write order — `SavePOD` (0x3C17EC, 1113 lines)

`SavePOD` mirrors the read order and is the tie-breaker for tag semantics. High-level sequence (each `WriteMarker(tag, len)` … `WriteMarker(tag|0x80000000)` for containers):
1. `0x3E8` version `"AB.POD.2.0"`.
2. `0x3E9` open scene.
   - background/ambient colour (0x7D0/0x7D1), `Num*` counts (0x7D2–0x7D9), then FPS (0x7E0).
   - for each camera → `0x7DA` block (8000–8004).
   - for each light → `0x7DB` block (7000–7007).
   - for each material → `0x7DF` block (3000–3026).
   - for each texture → `0x7DE` block (4000).
   - for each mesh → `0x7DC` block (6000–6020, each `CPODData` stream written as a nested 9000–9003 sub-block via the CPODData writer).
   - for each node → `0x7DD` block (5000–5016).
3. `0x800003E9` close scene.

A byte-exact writer must: (a) preserve each stream's original `eType`/`n`/`nStride`, (b) re-narrow indices to the stored index `eType`, (c) write only the channels/flags actually present, and (d) emit the same container nesting. See `09_...md` for where our `pod_writer.cpp` diverges.

## 7. Failure model

Every typed read returns 0/1; any failure aborts the whole parse with `return 1` and leaves the (partially built) model to be freed by the caller's `Destroy`. There is no partial-recovery path. Unknown tags are the *only* thing tolerated — they are skipped via `Skip(len)`.
