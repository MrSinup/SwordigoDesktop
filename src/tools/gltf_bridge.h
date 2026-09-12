#pragma once
// gltf_bridge.h — symmetric POD <-> glTF/GLB bridge.
//
// The POD tooling has two halves that must stay mirror-images of each other:
//   * gltf_export_glb()  — PODModel  -> .glb   (mesh, skin, animation, materials)
//   * gltf_import_glb()  — .glb      -> PODModel
//
// This bridge exposes that pair as a single symmetric, in-memory round-trip so
// callers (and the round-trip test) can go POD -> glTF -> POD without touching
// the filesystem-shaped signatures, and so both directions share ONE code path
// (the real, shipping export/import used by `bin/ruby --glb2pod` and the
// viewer). Keeping the round-trip anchored on the shipping functions is
// deliberate: it guarantees the test exercises exactly what the app runs, and
// avoids hand-building an arena-owned tg3_model (tg3_model's arrays are const,
// arena-owned; constructing one by hand and const-casting is fragile — parsing
// a GLB we just wrote is both safe and representative).
//
// Verified format contracts honoured by the underlying export/import (see
// docs/formats_and_schemas/pod_master and pod_writer.cpp/pod_loader.cpp):
//   * anim scale channel (node tag 5009) stride = (size % 7 == 0) ? 7 : 3.
//   * scene FPS is tag 2017 (2016 is a separate always-0 field); frames = 2009.
//   * per-vertex bone_indices -> mesh.bone_batches.indices[] -> POD node idx.
//   * indices UNSIGNED_SHORT when all < 65536 else 32-bit.
//   * nodes ordered: mesh nodes [0, num_mesh_nodes) then the rest.
//
// Standalone module. No OpenGL / ImGui dependency.

#include <cstdint>
#include <string>
#include <vector>

#include "gltf_glb.h" // GLTFTextureImage, GLTFImageBuffer, GLTFPBRInfo

namespace av {

struct PODModel;

// POD -> GLB bytes (in memory). `images` are the textures referenced by the
// model (may be empty). `flip_v` matches gltf_export_glb's default. Returns
// true on success; on failure returns false and fills `err` when non-null.
bool pod_to_glb(const PODModel& model,
                const std::vector<GLTFTextureImage>& images,
                std::vector<uint8_t>& out_glb,
                std::string* err = nullptr,
                bool flip_v = true);

// GLB bytes -> POD. Mirrors gltf_import_glb. `images`/`pbr` receive the
// embedded texture payloads and PBR material data as in gltf_import_glb.
bool glb_to_pod(const std::vector<uint8_t>& glb,
                PODModel& out,
                std::vector<GLTFImageBuffer>& images,
                std::string* err = nullptr,
                GLTFPBRInfo* pbr = nullptr,
                float scale = 1.0f);

// Convenience: full symmetric round-trip POD -> GLB -> POD, entirely in
// memory. Useful for validation/tests. Returns true on success.
bool pod_roundtrip_through_glb(const PODModel& in,
                               const std::vector<GLTFTextureImage>& images,
                               PODModel& out,
                               std::string* err = nullptr);

} // namespace av
