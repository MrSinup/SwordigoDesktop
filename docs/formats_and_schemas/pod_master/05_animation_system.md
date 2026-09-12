# 05 — Animation System

> **Derived from:** `CPVRTModelPOD::SetFrame` (0x3C0F10), `GetWorldMatrixNoCache` (0x3C0F40), `GetWorldMatrix` (0x3C130C — cache), `GetTransformationMatrix`/`GetRotationMatrix`/`GetScalingMatrix`/`GetTranslationMatrix` (0x3C10..), `FlushCache`/`InitImpl` (0x3C065C/0x3C0E82), and `sub_3BF6A0` node block close fix-up (`case 0x7DD` → `0x800007DD`).

## 1. Frame state — `SetFrame` (VERIFIED, full)

The animation cursor lives in the impl struct at `model[21]` (byte 84). `SetFrame(float f)`:

```c
if (this->nNumFrame /* this[19] */) {
    impl = this[21];
    impl[8] = (int)f;            // nFrame  = integer frame  (impl byte 8)
    impl[4] = f - (float)(int)f; // fBlend  = fractional part (impl byte 4)
} else {
    impl = this[21];
    impl[4] = 0;                 // no animation → blend 0
    impl[8] = 0;                 // frame 0
}
impl[0] = f;                     // fFrame  = raw frame (impl byte 0)
```

Impl (a.k.a. `CPVRTModelPODImpl`) layout recovered:
| Impl byte | Field | Meaning |
|---|---|---|
| 0 | `fFrame` | raw fractional frame just set |
| 4 | `fBlend` | fractional part (interpolation factor 0..1) |
| 8 | `nFrame` | integer (floor) frame |
| 12 | `pfCache` | per-node cache **frame-stamp** array (float*) |
| 16 | `pWmZeroCache` | frame-0 world-matrix cache (PVRTMATRIXf*) |
| 20 | `pWmCache` | animated world-matrix cache (PVRTMATRIXf*) |
| 24 | flag byte | set to 1 by `ReadFromMemory(SPODScene const&)` |

`InitImpl` allocates these: `pfCache = new float[nNumNode]` (`4*v3`), `pWmZeroCache`/`pWmCache = new[nNumNode*64]` (`v3<<6` = 64 bytes/matrix each), then `FlushCache`.

## 2. Node animation channels

Each `SPODNode` (see `02_...md`) carries independent channels, each optionally a full per-frame array plus an optional sparse index array:

| Channel | Default tag | Array tag | Index tag | Node ptr (byte) |
|---|---|---|---|---|
| Position (xyz) | 5004 | 5007 | 5013 | pfAnimPosition (+24), idx (+20) |
| Rotation (xyzw quat) | 5005 | 5008 | 5014 | pfAnimRotation (+32), idx (+28) |
| Scale (xyz) | 5006 | 5009 | 5015 | pfAnimScale (+40), idx (+36) |
| Matrix (4×4) | 5010 | 5011 | 5016 | pfAnimMatrix (+48), idx (+44) |

`nAnimFlags` (tag 5012, node+16) bitfield indicates which channels are animated. Bits observed being **set** at the node-close fix-up: `|1` (position present as animated), `|2` (rotation), `|4` (scale).

## 3. Node-close fix-up (VERIFIED)

At the node close tag (`0x800007DD`), when at least one animation array flagged `v148` was read, the reader normalises the *default* single-key channels into freshly-malloc'd 1-key arrays, or, if an animation array already exists, just marks the flag bit:

```c
if (position_animated_flag) {
    if (*pfAnimPosition /*node+24*/) nAnimFlags |= 1;           // array already present
    else { p = malloc(12); *pfAnimPosition = p; store default xyz (v182/v183); }  // 3 floats
    if (*pfAnimRotation /*node+32*/) nAnimFlags |= 2;
    else { p = malloc(16); *pfAnimRotation = p; store default quat; }               // 4 floats
    if (*pfAnimScale   /*node+40*/) nAnimFlags |= 4;
    else { p = malloc(28); *pfAnimScale = p; store default scale (7 floats); }       // 7 floats
}
```
*(sub_3BF6A0, case 0x7DD default→close.)* Note the **scale allocation is 0x1C = 28 bytes = 7 floats** — this matches our loader's comment `anim_scale; // size: 7 * num_frames (xyz + quat — SDK stores 7)` (`pod_loader.h`). Verified: the PowerVR scale channel stores 7 floats per key (3 scale + a 4-value scale-orientation/shear remainder).

## 4. Per-frame matrix composition

`GetWorldMatrixNoCache(mOut, node)` (VERIFIED):
```c
if (node->pfAnimMatrix /* +48 */) {
    GetTransformationMatrix(mOut, node);          // full 4x4 path (matrix channel)
} else {
    GetScalingMatrix(mOut, node);                 // S
    GetRotationMatrix(tmp, node);   mOut = mOut * tmp;   // S*R
    GetTranslationMatrix(tmp, node); mOut = mOut * tmp;  // S*R*T
}
if (node->nIdxParent /* +12 */ >= 0) {
    GetWorldMatrixNoCache(parentTmp, parent);
    mOut = mOut * parentTmp;                      // post-multiply parent world (recursive)
}
```

- **Matrix mode**: `GetTransformationMatrix` interpolates/loads the 16-float key at `nFrame` (and blends toward `nFrame+1` by `fBlend`).
- **TRS mode**: scale and translation interpolate **linearly**; rotation uses **quaternion slerp** (the separate `GetRotationMatrix` decompiles to a quaternion→matrix build; blend factor `fBlend`).
- Sparse channels: when an `*Idx` array is present, the key for `nFrame` is looked up through it before interpolation.

> The exact interpolation math inside `GetRotationMatrix`/`GetTransformationMatrix` (linear vs. slerp selection, normalisation) is only partially legible in the decompiler (heavy VFP). The **linear TRS + quaternion rotation** structure is established by the call graph and the `fBlend` usage; the precise slerp formula is **⚠️ UNVERIFIED** (tracked in `10_...md`). Standard PowerVR uses normalized-lerp for short arcs.

## 5. World-matrix cache — `GetWorldMatrix` (VERIFIED)

```c
impl = model[84];  node_ordinal = (nodePtr - pNode)/52;
if (impl->fFrame == 0.0f) {
    src = impl->pWmZeroCache;                     // frame-0 slot (byte 16)
} else if (impl->fFrame != impl->pfCache[node_ordinal] /* +12 */) {
    GetWorldMatrixNoCache(mOut, node);            // MISS → recompute
    impl->pfCache[node_ordinal] = impl->fFrame;   // stamp
    store mOut into impl->pWmCache[node_ordinal*64];
    return mOut;
} else {
    src = impl->pWmCache;                         // HIT (byte 20)
}
copy src[node_ordinal*64] → mOut;                 // 64-byte matrix
```

- **Frame 0** always reads the dedicated zero-cache (bind pose) — this is what `GetBoneWorldMatrix` exploits.
- For animated frames, `pfCache[node]` holds the frame stamp; a mismatch triggers a recompute + restamp; a match is a cheap 64-byte copy.
- Matrices are 64 bytes each (`<<6`), i.e. 16 floats.

## 6. `FlushCache`

`FlushCache` (called by `InitImpl`) invalidates every per-node stamp so the next `GetWorldMatrix` recomputes. It resets `pfCache` entries to a sentinel and re-seeds `pWmZeroCache` at frame 0. Any code that mutates node data must `FlushCache` (or re-`SetFrame`) to avoid stale world matrices.

## 7. FPS & frame count

- `nNumFrame` = scene tag 2009 (0x7D9, scene byte 76).
- FPS = scene tag **2016 (0x7E0)** in the reference read path (scene byte 80). Our loader reads FPS at 2017 — mismatch flagged in `09_...md`.
- `SetFrame(f)` accepts fractional `f`; `nFrame = floor(f)`, `fBlend = f - floor(f)`, so callers can drive smooth playback at arbitrary speed independent of FPS.
