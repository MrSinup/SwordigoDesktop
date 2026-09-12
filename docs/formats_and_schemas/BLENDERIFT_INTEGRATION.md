# blenderift Integration

`blenderift` (by a friend of the project, `blenderift-main/`) is a Swordigo
`.POD` → Blender toolchain: **jPOD.py** (a JSON/Python dump of any POD file,
derived from PowerVR's `jPOD` by Imagination Technologies' Developer Technology
Team) plus **fullscriptmaybe.py** (the Blender import script).

We did **not** ship the Python scripts — per project convention the useful
knowledge was re-implemented in C++ inside the Ruby SDK's existing POD
pipeline. This document records what came from where, and how it was verified.

## What was useful

| blenderift source | What it gave us | Where it landed |
|---|---|---|
| `jPOD.py` block table (PowerVR POD spec) | The complete authoritative block-ID reference (1000–9000 series): file, scene, material (incl. PBR 3028+), texture, node/animation, mesh, light, camera, vertex-block IDs | `src/tools/pod_loader.cpp` constants + comments |
| `jPOD.py` interleaved vertex layout | Confirms the 40-byte interleaved record: 9 floats (pos·3, nrm·3, uv·2, weight) + 1 int bone index at byte 32 | Already implemented; now cross-validated |
| `jPOD.py` non-interleaved reconstruction order | pos(6006) → nrm(6007) → uv(6010) → bone idx(6012) → weight(6013) | Already implemented; now cross-validated |
| `jPOD.py` MeshUnpackMatrix (6020) | The one real spec gap: our loader skipped it | NEW: parsed + applied in `readMeshBlock` (see below) |
| `fullscriptmaybe.py` matrix math | Reference POD→Blender axis convention (`YZInversion`, quaternion matrix signs) | Documented for the GLB round-trip; our POD→GLB path is Y-up/right-handed and matches Blender's native glTF handling |

## New capability: MeshUnpackMatrix (block 6020)

PowerVR-packed exports store vertex data packed and undo it through a per-mesh
16-float column-major matrix (`MeshUnpackMatrix`). The loader previously
skipped it (`default: off += len`).

- `PODMesh` gains `has_unpack_matrix` / `unpack_matrix[16]` / `mesh_type`
  (`src/tools/pod_loader.h`).
- `readMeshBlock` now parses 6020 and 6021 (`MeshType`), and applies the matrix
  to positions (w=1) and normals (rotation part, w=0) before the AABB pass —
  **unless** the matrix is identity or degenerate (all-zero), which covers all
  52 stock Swordigo PODs that carry a 6020 block (verified identity).

Because the matrix is baked at load time, every downstream consumer (viewer,
scene player, POD→GLB export) sees unpacked vertices, keeping the
Blender round-trip consistent.

## Cross-validation (our loader vs. jPOD.py)

Real assets were parsed by both implementations and compared:

| File | Mesh | Vertices | First vertex (pos/nrm/uv/bone/weight) | Faces |
|---|---|---|---|---|
| `hiro.POD` | 0 | 82 | `-2.958 63.067 -2.481 \| -0.052 0.997 -0.062 \| 2.654 1.388 \| 0 1.00` | 142, first `[0,1,2]` |
| `bat.POD` (non-interleaved) | 0 | 348 | `-0.864 22.009 -0.107 \| 0.000 1.000 -0.000 \| 0.192 0.364 \| 0 1.00` | 148, first `[0,1,2]` |

All values byte-for-byte identical. Swordigo faces are triangle **lists**
(strip count 0 everywhere), which our `parse_indices` handles.

## Credits

- **jPOD.py**: PowerVR by Imagination, Developer Technology Team
  (Copyright (c) Imagination Technologies Limited), as carried in
  `blenderift-main/blenderift/jPOD.py`.
- **blenderift**: its author (a friend of this project) — for the spec
  reference, the Blender import math, and for being the independent oracle
  used to validate our C++ loader.
- Our `pod_loader.cpp` remains an original C++ implementation (PowerVR Flash
  SDK lineage + libswordigo_arm32.c semantics), now cross-checked against the
  above.
