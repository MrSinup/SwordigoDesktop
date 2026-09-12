#pragma once
// rubymesh.h — RubyMesh binary (.rbm) collision-zone data layer.
//
// A .rbm file stores manually authored collision zones for one POD model,
// persistently, next to its scene. RubyMesh Workspace edits these files; the
// scene integration step ("Apply to Scene") turns enabled zones into native
// Swordigo GroundMesh + GroundPolygon objects.
//
// Pure data layer: no ImGui, no OpenGL, no external dependencies beyond the
// C++ standard library. All geometry is stored in the POD model's own space
// (authored pose, origin at the model origin); zone world_z is the scene-depth
// layer the zone's generated GroundMesh object will live on.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rbm {

// Surface materials stored in .rbm flags bits 3..7 (values 0..7).
enum class SurfaceMaterial : uint8_t {
    Stone = 0,
    Grass,
    Dirt,
    Wood,
    Metal,
    Sand,
    Ice,
    Custom
};

// One manual collision zone: a closed 2D polygon (world XY, model space) that
// becomes a Swordigo GroundPolygon + GroundMesh slab at `world_z`.
struct Zone {
    std::string  name;
    bool         enabled   = true;   // bit 0
    bool         invisible = true;   // bit 1 — use ruby_transparent.pvr in-game
    SurfaceMaterial material = SurfaceMaterial::Stone; // bits 3..7

    // Scene-depth layer for the generated object (the Depth tag) — Min/Max
    // depths below are relative offsets around it (vanilla convention).
    float        world_z   = 40.0f;
    float        depth_min = -45.0f;
    float        depth_max =  45.0f;

    // PVR texture stems ("" renders ruby_transparent at apply time).
    std::string  top_texture;
    std::string  front_texture;

    // Polygon outline, ordered (CCW after ensure_ccw). Model-space XY.
    std::vector<std::pair<float, float>> vertices;
};

// The whole authored zone set for one model.
struct RubyMesh {
    std::string model_name;
    std::vector<Zone> zones;
};

// ---------------------------------------------------------------------------
// File paths
// ---------------------------------------------------------------------------

// <scene_dir>/<model_stem>_rubymesh.rbm  (strips any directory components from
// the stem defensively).
std::string rbm_path_for(const std::string& model_stem,
                         const std::string& scene_dir);

// ---------------------------------------------------------------------------
// Polygon orientation helpers (pure, shared by the editor and Apply)
// ---------------------------------------------------------------------------

// Twice the signed area (positive = CCW when Y is up). 0 for <3 vertices.
double polygon_signed_area(const std::vector<std::pair<float, float>>& verts);

// Reverse the winding when the polygon is clockwise. No-op for <3 vertices.
void ensure_ccw(std::vector<std::pair<float, float>>& verts);

// ---------------------------------------------------------------------------
// I/O
// ---------------------------------------------------------------------------

// Load a .rbm file. Returns false with a human-readable `err` on any failure
// (missing file, wrong magic/version, checksum mismatch, truncation). Never
// throws; `out` is left untouched on failure.
bool rbm_load(const std::string& path, RubyMesh& out, std::string& err);

// Save a .rbm file atomically (temp file + rename) and verify the magic +
// checksum of the written file before returning true.
bool rbm_save(const RubyMesh& mesh, const std::string& path, std::string& err);

// rbm_load with a model_stem/scene_dir pair; on failure (missing or corrupt)
// returns an empty RubyMesh{model_name = stem} so callers can start fresh.
// The load error is printed to stderr when one occurred.
RubyMesh rbm_load_or_default(const std::string& model_stem,
                             const std::string& scene_dir);

} // namespace rbm
