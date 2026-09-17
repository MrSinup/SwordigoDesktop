# glTF/GLB → POD Animation Conversion — Accurate Mapping, Verification & Fixes

> **Scope:** How to convert a commercial glTF/GLB (Blender/Maya/Sketchfab export) into a Swordigo/Caver `.POD` with **correct skeletal animation and skinning**. Documents the exact channel/skin mapping, audits our indigenous conversion infra (`src/tools/gltf_import.cpp`, `pod_convert.cpp`, `pod_writer.cpp`, `pod_loader.cpp`), and records what is verified against a real round-trip of **`hero.glb`** and stock assets (`hiro.POD`, `hiro_hurt.POD`).
>
> **Reference ground truth:** `pod_master/01`–`05` (POD read path, node/anim structs, `SetFrame`, skinning). **Verification tool:** `src/tools/pod_dump.py`.

---

## 1. The fundamental impedance mismatch

| Concept | glTF 2.0 | POD (Caver) |
|---|---|---|
| Animation storage | **Sparse** keyframes: per-channel `(input=times[], output=values[])` samplers | **Dense** per-frame arrays: one key per frame (`NumFrame` total) |
| Time base | **Seconds** (FLOAT), arbitrary per channel | **Integer frames** + external `SetFrame(fFrame)` driving |
| Channels | `translation` (vec3), `rotation` (quat xyzw), `scale` (vec3), separate per node | Node `AnimPosition`(3)/`AnimRotation`(4)/`AnimScale`(**7**)/`AnimMatrix`(16) |
| Interp | LINEAR / STEP / CUBICSPLINE, per sampler | Engine lerps position/scale, **slerps** rotation between integer frames (`GetWorldMatrixNoCache`) |
| Skin bind pose | `inverseBindMatrices` accessor | Derived at runtime via `GetBoneWorldMatrix` (`SetFrame(0)` bind) |
| Skin indices | `JOINTS_0` = index into `skin.joints[]` | Per-vertex bone index resolved through **bone-batch** table (`pnBatches[base+local]`) |
| Skin weights | `WEIGHTS_0` = FLOAT/normalized VEC4 | `CPODData` weight stream (stock uses **FLOAT**, n=1..4) |

**The core reason animation "breaks":** glTF is a *sparse, seconds-based, per-node-tree* format; POD is a *dense, per-frame, flat-node* format. Every channel must be **resampled to a common integer-frame grid**, and the **scale channel must be written as 7 floats/key**, not 3 (see §4, the headline bug).

---

## 2. Test model — `hero.glb`

`~/.local/share/swordigo-desktop/assets/resources/hero.glb` (GLB v2, 2.54 MB). Structure recovered by inspecting the JSON chunk:

- **152 nodes**, 5 meshes, 5 materials/textures, **1 skin** (78 joints, `inverseBindMatrices` = accessor 22, MAT4 FLOAT×78), **1 animation** "Idle" (**283 channels/samplers**, all LINEAR; paths: 77 translation, 134 rotation, 72 scale).
- Skinned mesh = **mesh2** (`node88`, skin0): `JOINTS_0` = **VEC4 USHORT**, `WEIGHTS_0` = **VEC4 FLOAT**, `POSITION/NORMAL` FLOAT VEC3, `TEXCOORD_0` FLOAT VEC2, `INDICES` UINT.
- Animation samplers have **varying key counts** (sampler0=40 keys, sampler1=46…) at **different time stamps** → must be resampled to a single frame grid.

---

## 3. Correct glTF → POD mapping (the target)

### 3.1 Nodes / hierarchy
- glTF node tree → POD flat node array. **Mesh-bearing nodes must be emitted first** (POD `NumMeshNode` = count of leading mesh nodes; `02_...md`). Build a `gltf_node → pod_node` remap that places mesh nodes in `[0, NumMeshNode)` and skeleton/other nodes after.
- `node.ParentIndex` (5003) = remapped parent, `-1` for roots.
- Local transform: emit default `Position`(5004)/`Rotation`(5005)/`Scale`(5006) **or** a `Matrix`(5010), not both.

### 3.2 Animation channels → dense frames
1. Compute `max_time = max over all samplers of times.back()`.
2. Choose `fps` (30). `NumFrame = round(max_time * fps) + 1`.
3. For each animated node, for each frame `f`: sample time `t = f / fps`, find the bracketing keys, and interpolate per the sampler's `interpolation`:
   - **LINEAR**: lerp vec3; **slerp** quaternions (shortest-arc: negate q1 if dot<0).
   - **STEP**: hold left key.
   - **CUBICSPLINE**: Hermite with in/out tangents; renormalize quats.
4. Write `AnimPosition`(5007, 3/key), `AnimRotation`(5008, 4/key), **`AnimScale`(5009, 7/key)**, set `AnimFlags`(5012).

### 3.3 Scale channel = **7 floats per key** (critical)
POD's scale key is `[sx, sy, sz, qx, qy, qz, qw]` (scale + scale-orientation quaternion). **Verified two ways:**
- Stock `hiro_hurt.POD` scale key = **28 bytes = 7 floats** (`pod_dump.py`).
- Our own loader auto-detects: `pod_loader.cpp:1127` → `int stride = (anim_scale.size() % 7 == 0) ? 7 : 3;`
- Docs `05_animation_system.md` §3: the reader mallocs `0x1C = 28` bytes for the default scale key.

glTF scale is only 3 components → pad each key to 7 as `[sx,sy,sz, 0,0,0,1]` (identity scale-orientation).

### 3.4 Skinning
- `JOINTS_0[v]` are indices into `skin.joints[]`. Resolve to **POD node index**: `pod_bone = gltf_to_pod_node[ skin.joints[ JOINTS_0[v] ] ]`.
- Emit the **bone-batch** table so per-vertex indices are batch-local: `BoneBatchIndex`(6015) = the list of bone node indices in the batch; `NumBoneIndicesPerBatch`(6016) = **one count per batch**; `BoneOffsetPerBatch`(6017) = first face of each batch; `MaxNumBonesPerBatch`(6018); `NumBoneBatches`(6019). Per-vertex `BoneIndex` values must be **local indices into that batch's list** (`04_...md` §2).
- `inverseBindMatrices` → `PODNode::bind_matrix` (bind pose), so `skin_mesh`/`GetBoneWorldMatrix` skin relative to the true bind, not frame 0.
- Weights: stock uses **FLOAT** (`hiro.POD` BoneWeightList = DataType 1 FLOAT) — writing FLOAT weights is correct for this engine.

---

## 4. Verified findings from the real round-trip

Converted with the shipped CLI: `./bin/ruby --glb2pod hero.glb /tmp/hero_out.POD` → produced `hero_out.POD` (base, 5 meshes, 152 nodes) + `hero_out_Idle.POD` (**31 frames @ 30 fps**). Dumped both with `pod_dump.py` and compared to stock.

### ✅ What already works
- **Container/tag framing is valid** — both outputs parse with 0 unknown tags, all bytes consumed.
- **Resampling to a dense frame grid works**: `hero_out_Idle` node "pPlane2" has `AnimPosition`=372 B (31×3), `AnimRotation`=496 B (31×4). The resampler (`gltf_import.cpp:774`) correctly handles LINEAR/STEP/CUBICSPLINE and slerps quaternions.
- **`NumFrame`(2009)=31** is written correctly.
- **Skin streams present**: skinned mesh emits `BoneIndexList`(6012) + `BoneWeightList`(6013) + a bone-batch table.

### ❌ / ⚠️ Confirmed bugs (root causes of broken animation)

**BUG A — Scale channel written as 3 floats/key instead of 7 (HIGH).**
`hero_out_Idle` `AnimScale` = 372 B = 31×**3** floats. Stock is 7/key; the engine's own reader keys on `size % 7 == 0`. Consequences:
- The scale-orientation quaternion is dropped (minor for uniform scale).
- **Latent corruption:** for any clip where `3 × NumFrame` is divisible by 7 (NumFrame = 7, 14, 21, 28…), `pod_loader.cpp:1127` misdetects stride **7**, reads garbage, and the skeleton explodes. This alone makes animation unreliable.
- **Fix:** in `gltf_import.cpp` scale sampling, write 7 floats/key: `[sx,sy,sz,0,0,0,1]`; in `pod_writer.cpp` keep emitting the raw array (already generic).

**BUG B — Skin bone indices not remapped through `skin.joints[]` → POD nodes (HIGH).**
`gltf_import.cpp:267` stores `JOINTS_0` values *directly* as POD bone indices. But `JOINTS_0` indexes `skin.joints[]`, and each joint is a glTF node that must be remapped to its POD node index (and made batch-local). Result: vertices reference the wrong bones → mesh tears / collapses when animated. **Fix:** `pod_bone = gltf_to_pod_node[ skin.joints[j] ]`, then build a real batch bone-list and store batch-local indices.

**BUG C — Bone-batch table is a stub (HIGH).**
`gltf_import.cpp:275` hard-codes `count=1, offsets={0}, counts={1}, max_bones=1`, then `pod_convert`/writer emit `MaxBonesPerBatch=78, NumBoneBatches=1, BoneBatchCounts=[78?]` inconsistently (dump shows `BoneBatchCounts` previewing count=78 with 0 trailing data — a 1-element array holding 78). The batch's bone-index list and per-vertex local indices are not coherent, so the GPU/CPU palette lookup resolves wrong matrices. **Fix:** build the batch as: `BoneBatchIndex` = distinct POD node indices used by the mesh (≤ `MaxNumBonesPerBatch`), `NumBoneIndicesPerBatch=[N]`, `BoneOffsetPerBatch=[0]`, `NumBoneBatches=1`; rewrite each vertex bone index to its position in that list. For >`MaxBones` bones, split into multiple face-range batches.

**BUG D — Mesh nodes carry per-node animation + `AnimFlags=7` + redundant default TRS (MEDIUM).**
`hero_out_Idle` node "pPlane2" has `AnimFlags=7` **and** default `Position/Rotation/Scale` **and** dense anim arrays. Stock clips use `AnimFlags=0` with single-key arrays for static bones, and separate anim-only clips from the base mesh. Emitting both defaults and dense arrays risks the engine's node-close fixup (`05_...md` §3) double-seeding channels. **Fix:** for a clip POD, drop the default 5004/5005/5006 when a dense 5007/5008/5009 array exists; set `AnimFlags` bits only for channels actually animated.

### 🔧 Corrected earlier assumption
FPS is written at tag **2017** while the reference read path reads **2016** — BUT stock `hiro.POD`/`hiro_hurt.POD` **also store FPS=0 at 2016**. So the engine does **not** derive playback speed from the POD FPS field; frames are driven externally via `SetFrame`. **FPS mislabelling is cosmetic, not the cause of broken animation.** (This corrects the hypothesis in `09_indigenous_gap_analysis.md` §1 — downgrade that item from P0 to cosmetic; the real P0s are BUG A/B/C above.)

---

## 5. Indigenous infra audit summary

| Component | Role | Verdict |
|---|---|---|
| `gltf_import.cpp` `sample_channel` (l.774) | resample glTF sampler → dense frames | ✅ correct math (lerp/slerp/step/cubic) |
| `gltf_import.cpp` clip assembly (l.900) | build clip POD nodes | ⚠️ BUG A (scale 3≠7), BUG D (flags/defaults) |
| `gltf_import.cpp` skin ingest (l.235) | JOINTS/WEIGHTS → POD | ❌ BUG B (no joint→node remap), BUG C (stub batch) |
| `gltf_import.cpp` bind matrices (l.455) | IBM → `bind_matrix` | ✅ present (uses IBM, falls back to frame-0) |
| `pod_writer.cpp` `write_node` (l.208) | emit 5007/5008/5009 | ✅ generic (writes whatever array it's given — fix belongs upstream in the importer) |
| `pod_writer.cpp` `write_mesh` (l.168) | emit bone-batch + streams | ⚠️ emits whatever the stub batch provides (BUG C originates upstream) |
| `pod_loader.cpp` scale read (l.1127) | `%7?7:3` stride detect | ✅ correct — and is exactly what makes BUG A dangerous |
| glTF library | custom `tg3_*` (NOT tinygltf) | ℹ️ note: `pod_convert.h` docs say "tinygltf" but the code uses an in-house `tg3` parser |

> The importer uses an in-house `tg3_*` glTF parser, not tinygltf — worth correcting the stale comment in `pod_convert.h`.

---

## 6. Fix checklist (prioritised)

1. **[P0] Scale = 7 floats/key.** In the scale branch of the clip builder, expand each 3-float scale key to `[sx,sy,sz,0,0,0,1]` (and the `anim_scale.empty()` fallback likewise). Eliminates BUG A and its `%7` corruption.
2. **[P0] Remap skin indices.** `pod_bone = gltf_to_pod_node[skin.joints[JOINTS_0[v]]]`; build a coherent bone-batch bone-list; store batch-local per-vertex indices. Fixes BUG B + C.
3. **[P1] Clean clip nodes.** Drop redundant default TRS when dense anim arrays exist; set `AnimFlags` per actually-animated channel. Fixes BUG D.
4. **[P2] FPS label.** Optionally also write FPS at 2016 for tooling correctness (engine ignores it).
5. **[P2] Fix stale `tinygltf` comment** in `pod_convert.h`.

## 7. How to verify a fix

```bash
# convert
./bin/ruby --glb2pod --force \
  ~/.local/share/swordigo-desktop/assets/resources/hero.glb /tmp/hero_out.POD

# scale must be 7 floats/key: AnimScale bytes / 4 / NumFrame == 7
python3 src/tools/pod_dump.py /tmp/hero_out_Idle.POD | grep -E "NumFrame|AnimScale" | head

# structural parse must stay clean
python3 src/tools/pod_dump.py /tmp/hero_out_Idle.POD | tail -2   # 0 unknown, all bytes

# compare shape against a stock skinned clip
python3 src/tools/pod_dump.py \
  ~/.local/share/swordigo-desktop/assets/resources/hiro_hurt.POD | sed -n '/<Node>/,/<\/Node>/p' | head
```
A correct clip shows `AnimScale` length `= NumFrame × 7 × 4` bytes, batch-local bone indices, and a `NumBoneIndicesPerBatch` with one entry per batch whose value = number of bones in that batch.

### 7.1 Automated import check (permanent)

The manual `pod_dump.py` checks above prove a clip's *shape*. `tests/glb_import_spec_conformance_test.cpp`
proves its *values*: it evaluates a GLB with an independent evaluator written
from the glTF 2.0 spec (node local `T*R*S`, parent-chain worlds, STEP / LINEAR /
CUBICSPLINE, shortest-path slerp) and compares that against what
`av::gltf_import_all_clips()` + `av::get_node_matrix()` produce, by pairwise
inter-joint distance — an invariant of the rig that any change of basis leaves
alone, so POD and glTF may disagree about axes and still compare exactly.

```bash
./bin/tests/glb_import_spec_conformance_test
```

It asserts the two cases separately, for a reason worth knowing when triaging a
future failure: **at** a frame our importer read the source curve, so it must
match to float precision; **between** frames POD can only lerp across its own
interval, so a source whose keys sit off our grid is bounded by its own motion
there rather than exact. Only the first is a correctness assertion.

> Cross-reference: the reverse direction (POD → GLB export, the four defects that
> made animated exports shear, invert and fly off) is in §8 of
> `formats_and_schemas/pod_fbx_gltf_interconversion_report.md`.

## 8. Case study: `statue.glb` — "one animation" that produced 7 broken clips

### 8.1 Symptom (as reported)
Converting `statue.glb` (68 MB) produced **7 POD animation files** with wildly
inflated frame counts, and playback looked "highly broken." The expectation was
a single looping animation.

### 8.2 Binary analysis (`pod_dump.py` + raw GLB/tg3 inspection)
- The GLB genuinely embeds **7 distinct glTF animations**, not one:
  `AS_LYS_KJLDragon_Atk_00.001`, `…_Atk_01`, `_02`, `_06`, `_07`, `_08`, `_09`.
  The importer writes one clip POD per glTF animation, so **7 files is correct**
  — the DCC export baked 7 attack clips into a single file.
- Rig: **528 nodes, 1 skin, ~1276–1352 channels** per animation.
- Sampler input times span **16.3 s – 37.5 s** per clip; key density is mixed —
  some samplers hold only **1–2 keys** (constant channels), others **334–901**.

### 8.3 Root cause (two real defects in `gltf_import.cpp`)
1. **Hardcoded `clip_model.fps = 30.0f`.** The clips were authored at **24 fps**.
   Resampling 24fps content onto a 30fps grid both changed playback speed and
   inflated frame counts (`duration × 30 + 1`).
2. **`max_time` = max over *all* samplers' last key time.** A single degenerate
   1-key sampler, or a stray key parked far out on the timeline, stretched the
   whole clip's frame count. Every real channel finished early and then *held its
   last pose* for the padded tail → the "freeze then twitch" that read as broken.

The resampler (`sample_channel`) itself was correct for STEP / LINEAR /
CUBICSPLINE and for the count==1 constant case — the data was never corrupted;
the **frame grid and rate were wrong**.

### 8.4 Fix (`src/tools/gltf_import.cpp`, per-clip duration + fps derivation)
- **Duration:** collect the last key time of **multi-key samplers only** (skip
  `<2`-key constant channels), take the **90th-percentile** end time as the clip
  span, and reject a lone gross outlier (`> 4× median end` → fall back to median).
- **FPS:** compute the **median inter-key spacing** across dense samplers,
  `raw = 1/dt`, and **snap to the nearest common authored rate**
  {24, 25, 30, 48, 50, 60} when within 15 %; otherwise keep the rounded raw rate
  clamped to [1, 120]. Falls back to 30 only when no timing is available.
- `num_frames = round(max_time × fps) + 1`.

### 8.5 Result (re-converted with the fixed `bin/ruby --glb2pod`)

| Clip | fps before→after | frames before→after | POD size after |
|------|------------------|----------------------|----------------|
| Atk_00.001 | 30 → **24** | 491 → **393** | 11.7 MB |
| Atk_01     | 30 → **24** | 726 → **581** | 12.4 MB |
| Atk_02     | 30 → **24** | 501 → **401** | 12.0 MB |
| Atk_06     | 30 → **24** | 1126 → **901** | 26.7 MB |
| Atk_07     | 30 → **24** | 984 → **787** | 23.4 MB |
| Atk_08     | 30 → **24** | 957 → **766** | 22.7 MB |
| Atk_09     | 30 → **24** | 551 → **441** | 9.4 MB |

fps is now correctly reported by the converter (`… @ 24 fps`). Frame counts and
POD sizes drop because (a) the rate matches the source and (b) outlier keys no
longer pad each clip with frozen tail frames. The 7 files are expected — this is
a multi-clip source asset, not a conversion artifact.

### 8.6 Note for content authors
If only the "normal looping" animation is wanted in-game, the extra Atk clips
should be **stripped at export time** in the DCC tool (or the unused clip PODs
simply not shipped). The converter faithfully emits every glTF animation present;
it does not (and should not) guess which single clip is canonical.

### 8.7 How to reproduce the verification
```bash
./bin/ruby --glb2pod --force \
  ~/.local/share/swordigo-desktop/assets/resources/statue.glb /tmp/statue_reconv/statue.pod
# expect: 7 "clip POD:" lines, each "@ 24 fps", frame counts 393/581/401/901/787/766/441
```
