# 04 — Bones & Skinning (deepest)

> **Derived from:** `arm32_13/functions/CPVRTModelPOD/00000000003C16FC__CreateSkinIdxWeight.c`, `00000000003C1394__GetBoneWorldMatrix.c`, `00000000003C130C__GetWorldMatrix.c`, `00000000003C0F40__GetWorldMatrixNoCache.c`, `sub_3BF6A0` mesh block (bone-batch tags 6012–6019), `CPVRTBoneBatches::Release` (0x3C0FF8). Cross-checked `arm64_13`.

This is the most detailed file, per the mission. It reconstructs the full skinning pipeline from disk layout → bone batches → per-vertex packing → runtime skinned matrix.

## 1. Where skin data lives on disk

Two independent per-mesh data sets (all inside the `0x7DC` mesh block):

**Per-vertex skin streams** (`CPODData`, like any vertex attribute):
| Tag | Field | Meaning |
|---|---|---|
| 6012 | `sBoneIdx` (mesh+120) | up to 4 bone indices per vertex |
| 6013 | `sBoneWeight` (mesh+136) | up to 4 weights per vertex |

**Bone-batch table** (`CPVRTBoneBatches`, mesh+156..+176):
| Tag | Field (mesh byte) | Type | Meaning |
|---|---|---|---|
| 6015 | pnBatches (+156) | int32[] | flat list: the bone (node) indices used by each batch |
| 6016 | pnBatchBoneCnt (+160) | int32[] | number of bones in each batch |
| 6017 | pnBatchOffset (+164) | int32[] | first face (index-buffer offset) of each batch |
| 6018 | nBatchBoneMax (+168) | int32 | max bones per batch (GPU palette size) |
| 6019 | nBatchCnt (+172) | int32 | number of batches |

`CPVRTBoneBatches::Release` frees `pnBatches`, `pnBatchBoneCnt`, `pnBatchOffset` and zeroes the counts — confirming these three are heap arrays owned by the struct.

## 2. Why batches exist

A GPU vertex shader has a fixed-size bone-matrix palette (`nBatchBoneMax`). A mesh that references more than `nBatchBoneMax` distinct bones is **split into batches**: each batch is a contiguous run of faces (starting at `pnBatchOffset[b]`) that together reference at most `nBatchBoneMax` bones, listed in `pnBatches[b*nBatchBoneMax .. ]`. Per-vertex `sBoneIdx` values are **local indices into the current batch's bone list**, not global node indices. To resolve a global bone: `globalNode = pnBatches[batchBase + localIdx]`.

## 3. Per-vertex packing — `CreateSkinIdxWeight` (VERIFIED, full reconstruction)

`CreateSkinIdxWeight(pIdxOut[4], pWeightOut[4], numBones, pBoneIdx[], pfWeights[])` builds one vertex's packed 4-bone index+weight. Reconstructed pseudocode directly from the decompilation:

```c
// v19[0..3] = weights (bytes), v19[4..7] = indices
for (i = 0; i < numBones; ++i) {
    idx = pBoneIdx[i];
    v19[i+4] = idx;
    if (idx >= 256) { error("Too many bones (highest index is 255)."); return 1; }   // MAX 255 bones
    w = pfWeights[i];
    q = (int)(w * 255.0f);
    q &= ~(q >> 31);          // clamp negative to 0 (arithmetic-shift trick)
    if (q >= 255) q = 255;    // clamp to 255
    v19[i] = q;               // weight quantised to a byte
}
for (; i <= 3; ++i) { v19[i] = 0; v19[i+4] = 0; }   // pad unused slots to (weight 0, index 0)

if (numBones) {
    sum = v19[0]+v19[1]+v19[2]+v19[3];
    if (sum == 0) return 1;                 // reject: all-zero weights
    k = 0;
    while (sum <= 254) {                     // distribute rounding error until sum == 255
        if (v19[k]) { ++sum; v19[k] += 1; }  // only bump non-zero slots
        k = (k > 2) ? 0 : k + 1;             // round-robin over the 4 slots
    }
}
for (j = 0; j < 4; ++j) { pIdxOut[j] = v19[j+4]; pWeightOut[j] = v19[j]; }
return 0;
```

Key facts established:
- **Exactly 4 bones per vertex** (`UBYTE4` layout); unused slots = index 0, weight 0.
- **Weights are 8-bit** (`weight * 255`, clamped `[0,255]`), and are **renormalised so the four bytes sum to exactly 255** (the `while (sum <= 254)` loop). A shader divides by 255.
- **Bone index limit is 255** (indices are stored per byte).
- All-zero weight vectors are rejected (`sum == 0 → return 1`).

## 4. Runtime skinned matrix — `GetBoneWorldMatrix` (VERIFIED, full)

`GetBoneWorldMatrix(mOut, nodeIdx /*a3*/, boneIdx /*a4*/)` reconstructed verbatim:

```c
float savedFrame = this->fFrame;          // *this[21]
SetFrame(0.0f);                            // go to BIND POSE
GetWorldMatrix(mOut, nodeIdx);             // world of the *mesh node* at bind pose
GetWorldMatrix(tmp,  boneIdx);             // world of the *bone*      at bind pose
PVRTMatrixInverseF(tmp, tmp);              // inverse bind of the bone
PVRTMatrixMultiplyF(mOut, mOut, tmp);      // mOut = meshNodeBind * inverse(boneBind)
SetFrame(savedFrame);                      // restore animation frame
GetWorldMatrix(tmp, boneIdx);             // world of the bone at CURRENT frame
PVRTMatrixMultiplyF(mOut, mOut, tmp);      // mOut = meshNodeBind * inv(boneBind) * boneCurrent
```

So the final skinning matrix for one bone is:

```
Skin(bone) = World_bind(meshNode) · World_bind(bone)⁻¹ · World_current(bone)
```

The per-vertex skinned position is then `Σ_i weight_i · Skin(pnBatches[batchBase+idx_i]) · vertex`. This is the standard "bind-pose-relative" skinning, and it matches our loader's design note in `pod_loader.h` (`skin_mesh` "using POD bone batches and the engine's bind-pose-relative bone matrices"), including its warning to skin relative to the *true bind pose*, not animation frame 0 — which is exactly why `GetBoneWorldMatrix` re-captures bind pose via `SetFrame(0)` and multiplies by the current-frame bone world.

> Nuance worth noting for our merge path: the reference uses **`SetFrame(0.0)` (animation frame 0) as the bind pose**. Our `pod_loader.h` deliberately captures a *separate* `bind_matrix` when merging an animation-only POD onto a base mesh, because in that workflow frame 0 is the idle pose, not the export pose. Both are correct for their respective inputs; documented as a deliberate divergence in `09_...md`.

## 5. World-matrix cache interaction

`GetWorldMatrix` (see `05_animation_system.md` §cache) memoises world matrices per node per frame. `GetBoneWorldMatrix` calls `SetFrame` twice, which resets/updates the cache stamp — so bind-pose and current-frame lookups do not collide. The cache stores a dedicated **frame-0 ("zero") slot** (`impl+16`) distinct from the animated slot (`impl+20`), so repeated bind-pose queries are cheap.

## 6. src/tools status (summary; full matrix in 09)

`src/tools/pod_loader.{h,cpp}` implements: bone batches (`PODMesh::BoneBatch` with indices/counts/offsets/max), per-vertex `bone_indices`/`bone_weights` (`bones_per_vertex`), and `skin_mesh()` with bind-pose-relative matrices. Gaps to verify against this file: (a) 8-bit weight renormalisation-to-255 on **write**, (b) local-batch-index → global-node resolution, (c) the 255-bone hard limit. See `09_...md`.

## 7. glTF→POD bind-source selection (per-skin auto-detect)

The runtime skinning formula (`pod_loader.cpp` `skin_mesh`) is:

    skin[j] = inverse(worldTransform(meshNode)) · currentGlobal(j) · inverse(bind_world[j])

For the rest pose to cancel to identity, `bind_world[j]` must live in the **same
POD node-world space** that `get_node_matrix()` walks — i.e. the same space as
`worldTransform(meshNode)` and `currentGlobal(j)`. There are two candidate bind
sources and they are NOT interchangeable across assets:

| Source | Space | Correct for |
|---|---|---|
| `inverse(glTF inverseBindMatrices[j])` | the DCC/skin's authored space | rigs whose IBM already includes outer wrapper nodes (e.g. **statue.glb**, ×1258 armature baked into the IBM) |
| `rest_world[j]` — joint's static-TRS world walked up the POD parent chain | POD node space, always | rigs whose IBM **excludes** wrapper scale the POD hierarchy re-applies (e.g. **minecraft_bee.glb**, ×100 `Armature` node) |

Choosing one globally breaks the other family:
- Forcing `inverse(IBM)` → **bee** blows up ×100 (armature scale never cancels).
- Forcing `rest_world` → **statue** blows up ×1258 (its true bind ≠ its static TRS;
  the DCC baked a different bind into the IBM).

**Fix (`gltf_import.cpp`):** decide **per skin** by *simulating* the exact
`skin_mesh` matrix at the rest pose for BOTH candidates and measuring the
mean squared distance of `skin[j]` from identity. Whichever bind source drives
the rest-pose skin matrix closest to identity is the one that lives in POD space
and is selected for every joint of that skin. This is fully automatic — no
model-name special-casing, no heuristic scale thresholds.

Verified (frame-0 skinned-span ÷ bind-span ratio, `/tmp/skinverify/verify`):
- minecraft_bee.glb → **1.000 / 1.000 / 1.000** (rest_world chosen).
- statue.glb → **x = 1.000** on every mesh (IBM chosen); Y/Z > 1 on multi-key
  nodes is the genuine animation pose at frame 0, not a scale error.
- tung_tung_sahur_not_ai.glb → dominant-axis ≈ 1.0 (IBM chosen); spread is pose.

Note: per-part **rigid** skins (bee: 1 bone/vertex, each body-part mesh bound to
one bone) are the family most sensitive to this — the leftover `mesh_inverse`
term does not incidentally cancel a foreign-space IBM the way it does for a
single proper full-body skin.
