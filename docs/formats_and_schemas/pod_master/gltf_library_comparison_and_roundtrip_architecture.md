# glTF Library Reality Check + First-Class GLB↔POD Round-Trip Architecture

> **Question answered:** "Does our `ruby` actually use tinygltf? What does it use? How does it compare, and how do we build a fully modular GLB↔POD round-trip on top of the new POD research?"
>
> **Verified by reading:** `src/tools/tiny_gltf_v3.{h,c}`, `tiny_gltf_old.h`, `tinygltf_json_c.h`, `gltf_import.cpp`, `gltf_export.cpp`, `pod_convert.cpp`, `pod_writer.cpp`, `pod_loader.cpp`.

---

## 1. What `ruby` actually uses (the surprise)

**It does NOT use upstream tinygltf (the well-known C++ single-header).** It uses an **in-house C library named `tiny_gltf_v3`** whose API prefix is `tg3_`:

| File | Lines | Role |
|---|---|---|
| `src/tools/tiny_gltf_v3.h` | 4512 | Public C API (`tg3_*` structs + parse/write) |
| `src/tools/tiny_gltf_v3.c` | 3653 | Implementation |
| `src/tools/tinygltf_json_c.h` | 1743 | Bundled C JSON parser |
| `src/tools/tiny_gltf_old.h` | 2667 | Previous (v2) header, still present |

Its own banner (`tiny_gltf_v3.h:26`) says:

> *"Version: v3.0.0-alpha — Ground-up C-centric API rewrite of tinygltf."*
> Copyright *"Syoyo Fujita"* (the original tinygltf author), dated **2026**.

So `tg3` is a **C rewrite/successor of tinygltf**, not the upstream library. The `#include "tiny_gltf_v3.h"` in `gltf_import.cpp:7` is what the comment in `pod_convert.h` loosely called "tinygltf" — the comment is **stale/misleading** and should say `tiny_gltf_v3 (tg3)`.

### What `tg3` supports (verified from the header)
- Full glTF 2.0 object model: `tg3_accessor` (incl. **sparse**), `tg3_buffer_view` (byte_stride, draco flag), `tg3_mesh`/primitive (**morph targets**), `tg3_skin`, `tg3_animation` (channels/samplers), `tg3_material`, `tg3_camera`, KHR_lights_punctual, KHR_audio, `extras`/`extensions` as generic values.
- **Arena allocation** — one `tg3_model_free()` frees everything.
- **Structured errors** (`tg3_error_stack`), strictness levels, streaming callbacks, FS/image decode **off by default** (security-conscious).
- **A WRITER**: `tg3_write_to_memory()` / `tg3_write_to_file()` (`tiny_gltf_v3.h:1048/1054`) with `tg3_write_options`.

---

## 2. `tg3` vs upstream tinygltf — comparison

| Dimension | `tiny_gltf_v3` (tg3, ours) | upstream tinygltf (C++) |
|---|---|---|
| Language | **Pure C**, POD structs, no STL in API | C++ w/ `std::vector`/`std::map` everywhere |
| Memory | **Arena**; one free; fewer allocs | many heap allocs; RAII |
| Data access | zero-copy spans into arena | copies into STL containers |
| Sparse accessors | ✅ modelled | ✅ |
| Morph targets | ✅ modelled | ✅ |
| Draco | flag only (`draco_decoded`) — no built-in decode | needs external draco |
| Extensions | generic `tg3_value` tree + raw JSON | `Value` map |
| Writer | ✅ `tg3_write_to_*` | ✅ `WriteGltfSceneToFile` |
| Error reporting | structured stack + severities | string + bool |
| Security | URI/FS off by default, strictness knobs | FS on by default (footgun) |
| Embedding | 2 files, C ABI, no exceptions/RTTI | 1 header, heavy C++ |
| Ecosystem/testing | in-house, alpha | battle-tested, huge user base |

### Which is best — and in what ways
- **For an embedded game/tooling pipeline: `tg3` is the better fit** *architecturally* — pure C ABI, arena memory, no STL/exception overhead, security-off-by-default, and it already has a symmetric reader **and** writer. That symmetry is exactly what round-trip needs.
- **Upstream tinygltf wins on maturity**: it's battle-tested against thousands of real-world exporter quirks; `tg3` is `v3.0.0-alpha` and in-house, so its edge-case coverage is unproven.
- **Verdict:** keep `tg3` as the schema/IO layer (don't swap to upstream), but **treat it as the single source of truth for BOTH directions** and harden it with a conformance test corpus. The problem today is not the library — it's that **we only use half of it** (see §3).

---

## 3. The actual round-trip gap (root cause)

Our pipeline is **asymmetric and hand-split**:

```
GLB  ──tg3_parse──▶  tg3_model  ──(hand code)──▶  PODModel  ──pod_writer──▶  .POD
                     gltf_import.cpp

.POD ──pod_loader──▶ PODModel   ──(hand JSON)──▶  glTF JSON  ─────────────▶  GLB
                                  gltf_export.cpp  (741 lines, manual JsonWriter, snprintf)
```

- **Import** uses `tg3` (good) but flattens straight into `PODModel`, losing glTF structure (skins, node tree, sparse) and introducing the bugs in `glb_to_pod_animation_conversion.md` (scale 3≠7, unremapped joints, stub bone-batch).
- **Export** (`gltf_export.cpp`) **does NOT use `tg3`'s writer at all** — it hand-writes glTF JSON with a bespoke `JsonWriter` (`num()`/`snprintf`/manual string escaping). So the two halves share **no schema**: a field imported one way can be exported a different way, and neither is checked against the other.
- There is **no `PODModel` ↔ `tg3_model` bridge**. Everything goes through the lossy `PODModel` intermediate, so anything `PODModel` can't represent (multiple UV sets, morph targets, cameras, full PBR, sparse) is silently dropped both ways.

**This is why round-trip isn't first-class:** two independent, un-cross-checked codepaths over a lossy middle struct.

---

## 4. Proposed modular architecture (first-class GLB↔POD round-trip)

Build a **symmetric, layered bridge** with `tg3_model` as the canonical glTF representation and the **POD research (`pod_master/01`–`05`) as the authoritative POD schema**. Five small modules, each independently testable:

```
          ┌───────────────────────────────────────────────────────────┐
          │  tg3_model  (canonical glTF 2.0 — read AND written by tg3) │
          └───────────────▲───────────────────────────┬───────────────┘
              tg3_parse    │                           │  tg3_write_to_file
                           │                           ▼
     .glb / .gltf ─────────┘                    .glb / .gltf
                           ▲                           │
        (A) gltf_bridge_import  │           │  (B) gltf_bridge_export
                           │                           ▼
          ┌───────────────┴───────────────────────────┴───────────────┐
          │  PodScene  (faithful POD model: nodes, CPODData streams,   │
          │  bone-batches, 7-float scale keys, dense frames — mirrors  │
          │  pod_master structs exactly)                               │
          └───────────────▲───────────────────────────┬───────────────┘
             pod_read      │                           │  pod_write
                           │                           ▼
                        .POD  ◀───────────────────────  .POD
```

### Module 1 — `pod_schema` (the contract)
A single header that encodes the **verified** POD layout from `pod_master`: CPODData `{eType,n,stride,data}`, the tag IDs, **scale = 7 floats/key**, bone-batch semantics, node ordering (mesh nodes first). Both `pod_read` and `pod_write` include only this. Kills drift like the FPS-tag and scale-stride bugs by construction.

### Module 2 — `pod_io` (replace ad-hoc reader/writer)
`pod_read(bytes) → PodScene` and `pod_write(PodScene) → bytes`, both driven by `pod_schema`. Add an **internal round-trip assertion**: `pod_write(pod_read(x))` must byte-match `x` for stock assets (validate against the 423-file corpus with `pod_dump.py`).

### Module 3 — `gltf_bridge` (the missing piece)
Two pure functions over the **canonical `tg3_model`**, sharing one joint/node remap and one channel-resampler:
- `tg3_to_pod(tg3_model, opts) → PodScene`
- `pod_to_tg3(PodScene, opts) → tg3_model`  ← then serialize with **`tg3_write_to_file`** (retire the hand-rolled `JsonWriter` in `gltf_export.cpp`).

Centralize here: node-tree→flat remap (+inverse), **animation resampling** (sparse↔dense at fps), **scale 3↔7 packing**, skin **joints[]↔POD-node** remap, and **bone-batch build/split**. One implementation, used both directions → symmetry guaranteed.

### Module 4 — `gltf_facade` (thin public API)
`glb_to_pod(path,opts)` / `pod_to_glb(path,opts)` = `tg3_parse → tg3_to_pod → pod_write` and the reverse. `pod_convert.cpp` keeps only texture encoding + CLI; all model logic moves into the bridge.

### Module 5 — `roundtrip_test` (proof harness)
CI-style checks:
1. `pod → glb → pod` structural identity (nodes, frames, streams).
2. `glb → pod → glb` skeleton/anim identity within epsilon (sample every node's world matrix per frame; compare to source `tg3` sampled values).
3. `pod_dump.py` clean-parse over converted output.
4. Skinned-mesh vertex positions at frames {0, mid, last} match the source glTF skinning within tolerance.

---

## 5. Concrete migration steps

1. **Rename the lie:** fix the `tinygltf` comment in `pod_convert.h` → `tiny_gltf_v3 (tg3)`.
2. **Adopt `tg3`'s writer:** replace `gltf_export.cpp`'s hand-rolled JSON with `pod_to_tg3()` + `tg3_write_to_file()`. Immediately removes an entire class of export/import schema drift.
3. **Introduce `PodScene` + `pod_schema`** mirroring `pod_master`; port `pod_writer.cpp`/`pod_loader.cpp` onto it (fix scale=7/key and bone-batch there once).
4. **Centralize conversion in `gltf_bridge`** (fixes A/B/C/D from `glb_to_pod_animation_conversion.md` in one place, used both ways).
5. **Wire `roundtrip_test`** against `hero.glb` and the stock 423-POD corpus; gate future changes on it.

### What "first-class" buys you
- **Lossless where POD allows** (nodes, skin, dense anim, materials, multi-UV) and **explicit, logged loss** where it can't (morph targets, cameras) instead of silent drops.
- A single resampler/remapper → the scale-stride and joint-remap bugs can't reappear in only one direction.
- The POD research docs become executable: `pod_schema` is the doc, in code.

---

## 6. Summary

- Our `ruby` uses **`tiny_gltf_v3` (tg3)** — an in-house pure-C rewrite of tinygltf, **not** upstream tinygltf. It is feature-complete (sparse, morph, skins, animations) and **has both a reader and a writer**.
- `tg3` is the right choice to keep (C ABI, arena, security-off-by-default, symmetric IO); upstream tinygltf's only real edge is maturity.
- The round-trip is broken not because of the library but because we **only use tg3 for reading** and **hand-roll the glTF writer**, with a lossy `PODModel` in the middle and duplicated, un-cross-checked conversion logic.
- The fix is a **symmetric 5-module bridge** (`pod_schema`, `pod_io`, `gltf_bridge`, `gltf_facade`, `roundtrip_test`) that makes `tg3_model` the canonical glTF form and the `pod_master` research the canonical POD schema — with a round-trip test harness proving identity in both directions.
