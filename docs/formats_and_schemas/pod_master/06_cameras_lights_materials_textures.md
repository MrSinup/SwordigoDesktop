# 06 — Cameras, Lights, Materials & Textures

> **Derived from:** `sub_3BF6A0` blocks `case 0x7DA` (camera), `0x7DB` (light), `0x7DE` (texture), `0x7DF` (material); struct sizes from `CopyFromMemory` (`PVRTModelPODCopy*` strides).

Struct sizes recovered from `CopyFromMemory`: **SPODCamera = 20**, **SPODLight = 40**, **SPODTexture = 4**, **SPODMaterial = 156** bytes.

## 1. SPODCamera — 20 bytes (block 0x7DA / 2010)

Reader sets `v145 = pCamera + 20*i; v145[16] = 0` then reads (`sub_3BF6A0`):

| Byte | DWORD | Field | Type | Tag |
|---|---|---|---|---|
| 0 | 0 | nIdxTarget (target node index, -1 none) | int32 | 8000 |
| 4 | 1 | fFOV | float | 8001 |
| 8 | 2 | fFar | float | 8002 |
| 12 | 3 | fNear | float | 8003 |
| 16 | 4 | pfAnimFOV | float* | 8004 (`ReadAfterAlloc32`) |

`GetCamera` / `GetCameraPos` (0x3C143C / 0x3C1534) resolve a camera to a world position by walking the target node's world matrix.

## 2. SPODLight — 40 bytes (block 0x7DB / 2011)

Reader sets `v144 = pLight + 40*i` (`sub_3BF6A0`, `case 0x7DB`):

| Byte | DWORD | Field | Type | Tag |
|---|---|---|---|---|
| 0 | 0 | nIdxTarget | int32 | 7000 |
| 4 | 1..3 | pfColour | float[3] | 7001 (`ReadArray32(3)`) |
| 16 | 4 | eType (`EPODLight`) | int32 enum | 7002 |
| 20 | 5 | fConstantAttenuation | float | 7003 |
| 24 | 6 | fLinearAttenuation | float | 7004 |
| 28 | 7 | fQuadraticAttenuation | float | 7005 |
| 32 | 8 | fFalloffAngle | float | 7006 |
| 36 | 9 | fFalloffExponent | float | 7007 |

`GetLight`/`GetLightPosition`/`GetLightDirection` (0x3C15F0 / 0x3C1668 / 0x3C16B4) resolve world position/direction through the target node.

`EPODLight` enum values (point/directional/spot) are **⚠️ UNVERIFIED** numerically — see `10_...md`.

## 3. SPODTexture — 4 bytes (block 0x7DE / 2014)

| Byte | DWORD | Field | Type | Tag |
|---|---|---|---|---|
| 0 | 0 | pszName (filename) | char* | 4000 (`ReadAfterAlloc`) |

A texture is just a filename string; materials reference textures by index.

## 4. SPODMaterial — 156 bytes (block 0x7DF / 2015)

Reader sets `v33 = pMaterial + 156*i`, then **`memset(v33+4, 0xFF, 0x28)`** (initialises 10 int fields at bytes 4..43 to `-1`, i.e. texture indices default "none"), seeds two default float triples (`loc_3C0218`/`loc_3C0220` at bytes 96/104) and two blend enums to `32774` at bytes 112/116. Recovered field offsets (`sub_3BF6A0`, `case 0x7DF`):

| Byte | DWORD | Field | Type | Tag |
|---|---|---|---|---|
| 0 | 0 | pszName | char* | 3000 |
| 4 | 1 | nIdxTexDiffuse | int32 (default -1) | 3001 |
| 8 | 2 | nIdxTexAmbient | int32 (default -1) | (memset 0xFF) |
| 12 | 3 | nIdxTexSpecularColour | int32 (-1) | 3009 group |
| 16..40 | 4..10 | further texture indices (bump/emissive/gloss/opacity/reflection/refraction) | int32 (-1) | 3010–3017 |
| 44 | 11 | fOpacity | float | 3002 |
| 48 | 12..14 | pfMatAmbient | float[3] | 3003 |
| 60 | 15..17 | pfMatDiffuse | float[3] | 3004 |
| 72 | 18..20 | pfMatSpecular | float[3] | 3005 |
| 84 | 21 | fMatShininess | float | 3006 |
| 88 | 22 | pszEffectFile | char* | 3007 |
| 92 | 23 | pszEffectName | char* | 3008 |
| 96 | 24..25 | pfBlendColour (default) | float[2]/blend | 3018–3021 (`EPODBlendFunc`) |
| 104 | 26..27 | pfBlendFactor (default) | float[2] | 3020/3021 |
| 112 | 28 | eBlendSrcRGB (default 32774) | `EPODBlendOp` | 3022 |
| 116 | 29 | eBlendDstRGB (default 32774) | `EPODBlendOp` | 3023 |
| 120 | 30..33 | pfBlendColour[4] | float[4] | 3024 (`ReadArray32(4)`) |
| 136 | 34..37 | pfBlendFactor[4] | float[4] | 3025 |
| 152 | 38 | nFlags | uint32 | 3026 |

> The precise mapping of tags **3009–3021** onto the individual blend-state and texture-index fields is **⚠️ UNVERIFIED** at name granularity — the decompiler exposes the *target byte offsets* (cited above via `v116=v33+48`, `v113=v33+60`, `v110=v33+72`, `v107=v33+84`, `v104=v33+88`, `v102=v33+92`, `v96=v33+100`, `v100=v33+104`, `v98=v33+108`, `v134=v33+112`, `v122=v33+116`, `v131=v33+120`, `v128=v33+136`, `v125=v33+152`) but not the PowerVR field names. Names follow PowerVR SDK `SPODMaterial`. Blend enum default `32774 = 0x8006 = GL_FUNC_ADD`, consistent with an `EPODBlendOp`.

## 5. Indigenous coverage

Our loader (`pod_loader.cpp`) handles only **material** tags 3000, 3001, 3002, 3004 and **texture** tag 4000. It has **no camera (0x7DA) or light (0x7DB) parsing** and ignores most material blend/spec fields. This is acceptable for a game renderer that lights scenes itself, but it means cameras, lights, and full material state cannot round-trip. Tracked in `09_...md`.
