# 02 — Scene Header & Node Hierarchy

> **Derived from:** `arm32_13/functions/CPVRTModelPOD/00000000003C06D8__CopyFromMemory.c` (struct sizes & scene-header offsets), `sub_3BF6A0` node block (`case 0x7DD`), `GetWorldMatrixNoCache` (parenting). Cross-checked with `arm64_13`.

## 1. SPODScene / model header

`CopyFromMemory` copies `0x54` (84) bytes of header, then the pointer/count pairs. All offsets below are **byte** offsets into the scene object; the decompiler indexes it as a DWORD array `a2[i]`.

| Byte | DWORD idx | Field | Type | Evidence |
|---|---|---|---|---|
| 0 | 0..2 | Colour background | float[3] | `for(i<3) a1[i]=a2[i]` |
| 12 | 3..5 | Colour ambient | float[3] | `a1[i+... ] (v5+12)` |
| 24 | 6 | nNumCamera | uint32 | `v166 = v9+6`, `SafeAlloc<SPODCamera>` |
| 28 | 7 | pCamera | SPODCamera* | `v160 = v9+7` |
| 32 | 8 | nNumLight | uint32 | `v165 = v9+8` |
| 36 | 9 | pLight | SPODLight* | `v159 = v9+9` |
| 40 | 10 | nNumMesh | uint32 | `v164 = v9+10`; `a2[10]` in copy loop |
| 44 | 11 | pMesh | SPODMesh* | `v157 = v9+11`; `a2[11]` |
| 48 | 12 | nNumNode | uint32 | `a2[12]`, `*(a1+48)` |
| 52 | 13 | nNumMeshNode | uint32 | tag 0x7D6 `v9+13` |
| 56 | 14 | pNode | SPODNode* | `v156 = v9+14`; `a2[14]` |
| 60 | 15 | nNumTexture | uint32 | `v162 = v9+15` |
| 64 | 16 | pTexture | SPODTexture* | `v155 = v9+16` |
| 68 | 17 | nNumMaterial | uint32 | `v163 = v9+17` |
| 72 | 18 | pMaterial | SPODMaterial* | `v158 = v9+18`; `a2[18]` |
| 76 | 19 | nNumFrame | uint32 | tag 0x7D9 `v9+19`; copy `*(a1+76)=*(a2+19)` |
| 80 | 20 | nFPS / flags byte | uint32 | tag 0x7E0 `v9+20`; byte 80 tested `*((u8*)v9+80)` for fixed-point |
| 84 | 21 | pCache (`CPVRTModelPODImpl*`) | ptr | allocated by `InitImpl` (`this[21]`) |

Total copied header = 84 bytes (`qmemcpy 0x54` in `ReadFromMemory(SPODScene const&)`), plus the impl pointer at 84.

**Byte 80 low bit = "interleaved data is fixed-point"**: at scene close, `if (*((u8*)v9+80) << 31) PVRTModelPODToggleFixedPoint(v9);` (`sub_3BF6A0`, `LABEL_226` region).

### Scene-close validation
Before accepting the file, the reader asserts the number of each block it actually read equals the declared count (`sub_3BF6A0` `case -2147482647`):
```
v152 == *v166 (cameras)  && v151 == *v165 (lights) && v154 == *v163 (materials)
&& v153 == *v164 (meshes) && v150 == *v162 (textures) && v149 == *v161 (nodes)
```
Any mismatch → `return 1` (failure). This is a strong integrity guarantee we can mirror.

## 2. SPODNode — 52 bytes

Node stride is **52 bytes** (`PVRTModelPODCopyNode(..., +52 per node)`; `SafeAlloc<SPODNode>(a1+56, 52*v7)`). Byte offsets from the node block set-up in `sub_3BF6A0` `case 0x7DD` (`v64 = base + 52*i`; each `v64 + k` is DWORD index `k`):

| Byte | DWORD | Field | Type | Tag | Evidence |
|---|---|---|---|---|---|
| 0 | 0 | nIdx (object index) | int32 | 5000 | `v130 = v64` |
| 4 | 1 | pszName | char* | 5001 | `v127 = v64+1` `ReadAfterAlloc` |
| 8 | 2 | nIdxMaterial | int32 | 5002 | `v124 = v64+2` |
| 12 | 3 | nIdxParent | int32 | 5003 | `v121 = v64+3` |
| 16 | 4 | nAnimFlags | uint32 | 5012 | `v142 = v64+4` |
| 20 | 5 | pnAnimPositionIdx | uint32* | 5013 | `v118 = v64+5` |
| 24 | 6 | pfAnimPosition | float* | 5007 / 5004 | `v133 = v64+6` |
| 28 | 7 | pnAnimRotationIdx | uint32* | 5014 | `v115 = v64+7` |
| 32 | 8 | pfAnimRotation | float* | 5008 / 5005 | `v136 = v64+8` |
| 36 | 9 | pnAnimScaleIdx | uint32* | 5015 | `v112 = v64+9` |
| 40 | 10 | pfAnimScale | float* | 5009 / 5006 | `v139 = v64+10` |
| 44 | 11 | pnAnimMatrixIdx | uint32* | 5016 | `v109 = v64+11` |
| 48 | 12 | pfAnimMatrix | float* | 5011 / 5010 | `v106 = v64+12` |

`GetWorldMatrixNoCache` reads **node+48** (`pfAnimMatrix`, DWORD 12) to decide matrix-vs-TRS, and **node+12** (`nIdxParent`, DWORD 3) for recursion — cross-confirming the offsets:
```c
if ( *(_DWORD *)(a3 + 48) )          // pfAnimMatrix present → GetTransformationMatrix
    GetTransformationMatrix(...);
else { GetScalingMatrix; GetRotationMatrix; GetTranslationMatrix; }   // S*R*T
if ( *(int *)(a3 + 12) >= 0 )        // nIdxParent >= 0 → multiply by parent world
    GetWorldMatrixNoCache(parent); PVRTMatrixMultiplyF(...);
```

## 3. Parenting & ordering rules

- **`nIdxParent`** (byte 12): `-1` for a root; otherwise the index of the parent node in the flat node array. World matrix = `local * parentWorld` (post-multiply, applied recursively — `GetWorldMatrixNoCache`).
- **Mesh nodes come first**: the scene stores `nNumMeshNode` (2006). The first `nNumMeshNode` entries of the node array are mesh-bearing nodes; a node's `nIdx` is its object (mesh/camera/light) index. This matches `src/tools/pod_loader.h` `num_mesh_nodes` ("first num_mesh_nodes nodes are mesh nodes").
- A node references its mesh/camera/light through `nIdx` and its material through `nIdxMaterial`.

## 4. Static vs. animated transform selection

The node carries both a *default* channel value (tags 5004/5005/5006/5010) and a *full animation array* (5007/5008/5009/5011). At the node close tag the reader normalises these — see `05_animation_system.md` §"Node close fix-up". `pfAnimMatrix != 0` forces the whole node to matrix mode; otherwise scale∘rotation∘translation is used.
