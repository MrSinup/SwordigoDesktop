#pragma once
// scene_collision_mesh.h — transparent-PVR asset helper for invisible
// collision meshes.
//
// The original "Populate with Collision Mesh [EXPT]" auto-slicer lived here
// (Z-slice POD → convex hull → boulder blobs) but was replaced by the
// RubyMesh Workspace (manual zone authoring, see tools/rubymesh_editor.*).
// What survives is the tiny asset helper the Apply pipeline still needs:
// ruby_transparent.pvr, the fully transparent texture that makes generated
// GroundMesh objects invisible in-game.

#include <string>

namespace col_mesh {

// ---------------------------------------------------------------------------
// Ensure ruby_transparent.pvr exists in scene_dir.
// If the file is missing, writes an 80-byte PVR v2 RGBA8888 4×4 all-zeros
// asset. Returns the full path on success, empty string on failure.
// Thread-safe to call from a worker (write is atomic via temp-file rename).
// ---------------------------------------------------------------------------
std::string ensure_transparent_pvr(const std::string& scene_dir);

} // namespace col_mesh
