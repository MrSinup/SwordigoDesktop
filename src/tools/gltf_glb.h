#pragma once
// gltf_glb.h — glTF 2.0 / GLB round-trip for POD assets
//
// gltf_export_glb(): serialize a PODModel (mesh, skin, animation, materials)
// into a self-contained .glb (JSON + binary chunks, textures embedded as PNG).
// gltf_import_glb(): parse a .glb back into a PODModel so edits made in
// Blender can be written back to .pod + .pvr game assets.
//
// Standalone modules. No OpenGL, ImGui, Blender or external JSON dependency.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <array>

namespace av {

struct PODModel;

// RGBA texture for embedding into the GLB. `name` matches an entry in
// PODModel::texture_filenames (bare filename, no directory).
struct GLTFTextureImage {
    std::string name;
    std::vector<uint8_t> rgba;
    int w = 0;
    int h = 0;
};

// Serialize `model` to `output_path` as a GLB file. `images` are the textures
// referenced by the model; missing entries simply leave materials untextured.
// `flip_v` flips the V coordinate (v = 1 - v) so exported GLBs display textures
// correctly in standard DCC tools (Blender, etc.), which expect top-origin UVs.
// POD UVs are bottom-origin (v = 0 at bottom), so flip_v defaults to true.
bool gltf_export_glb(const PODModel& model,
                     const std::vector<GLTFTextureImage>& images,
                     const std::string& output_path,
                     std::string* err = nullptr,
                     bool flip_v = true);

// A texture image extracted from a GLB (PNG or JPEG payload) ready for
// conversion back into game texture formats (e.g. .pvr / .tex.png).
struct GLTFImageBuffer {
    std::string mime;            // "image/png" | "image/jpeg"
    std::vector<uint8_t> data;
};

// PBR material data recovered from the glTF materials (parallel to
// PODModel::materials). Texture slots reference images by glTF image index
// (see GLTFPBRInfo::image_gltf_index); -1 = no map. metallic-roughness is a
// single combined texture per glTF (B = metalness, G = roughness); the caller
// swizzles channels when building GL textures.
struct GLTFPBRMaterial {
    float base_color[4] = {1, 1, 1, 1};
    float metallic = 1.0f;
    float roughness = 1.0f;
    float occlusion = 1.0f;
    float emissive[3] = {0, 0, 0};
    int base_tex = -1;             // glTF image index
    int metalrough_tex = -1;       // glTF image index (combined B/G)
    int normal_tex = -1;           // glTF image index
    int occl_tex = -1;             // glTF image index
    int emissive_tex = -1;         // glTF image index
    float normal_scale = 1.0f;
    float alpha_cutoff = 0.5f;
    int alpha_mode = 0;            // 0 opaque, 1 mask, 2 blend
    bool double_sided = false;
};

// Full PBR material info + the image payloads the slots reference.
struct GLTFPBRInfo {
    struct Image {
        std::string mime;               // "image/png" | "image/jpeg"
        std::vector<uint8_t> data;
        // True when the glTF sampler referencing this image requests
        // GL_NEAREST (pixel-art assets). The viewer honours it instead of
        // forcing trilinear + anisotropic filtering (A3).
        bool nearest = false;
    };
    std::vector<GLTFPBRMaterial> materials;   // parallel to PODModel::materials
    std::vector<int> image_gltf_index;        // glTF image index of each payload
    std::vector<Image> images;                // deduped payloads (referenced only)
};

// Parse a GLB file into a PODModel. Embedded images are returned in `images`
// (in material/texture order). When @p pbr is non-null it is filled with the
// PBR material data (factors, map image indices, and the referenced payloads).
// Returns true on success.
bool gltf_import_glb(const std::string& path,
                      PODModel& out,
                      std::vector<GLTFImageBuffer>& images,
                      std::string* err = nullptr,
                      GLTFPBRInfo* pbr = nullptr,
                      float scale = 1.0f,
                      bool rigid_skin = false);

// S1: when rigid_skin is true, each vertex is pre-baked to its dominant
// (max-weight) joint at weight 1.0 — matching the game's C_Matrix4Vector3ArraySkin,
// which reads ONE bone index per vertex and ignores weights. Leave false for the
// viewer (full weights preview accurately there).

// ── S2: rigid-bone selection scored against the clips' motion ──────────────
//
// The engine's C_Matrix4Vector3ArraySkin reads ONE bone per vertex, so a smooth
// glTF rig has to be collapsed onto a single influence. Picking the max-weight
// bone does that at the BIND pose, which is a choice made while the model is
// standing still — the vertex may belong to a bone that barely moves, while a
// neighbour that owns an eighth of its weight carries the whole limb.
//
// This re-picks each vertex's bone by measuring, over the supplied clips, which
// of that vertex's own influences reproduces the smooth result best:
//
//     cost(j) = sum over sampled frames of | skin(j,f)*p - sum_k w_k*skin(j_k,f)*p |^2
//
// Measured on soldier-v1 `walk` (59,674 verts, 27 frames): the worst-case
// per-vertex deviation falls from 4.10% of the model diagonal to 2.28%, mean by
// 11.7%, with 18.3% of vertices changing bone. Nothing about the POD format or
// the engine contract changes — the same one-bone-per-vertex bake, chosen
// better. With `clips` empty this falls back to max-weight, i.e. no change.
//
// Call it on a model whose meshes still carry full weights (import with
// rigid_skin = false) and it collapses them; call it again and it is a no-op.
struct RigidSkinRefineStats {
    int    vertices     = 0;
    int    moved        = 0;      // vertices whose bone changed
    int    pose_samples = 0;      // frames actually scored against
    double mean_before  = 0.0;    // metres, averaged over those samples
    double mean_after   = 0.0;
    double worst_before = 0.0;
    double worst_after  = 0.0;
};

bool refine_rigid_skin(PODModel& model,
                       const std::vector<std::pair<std::string, PODModel>>& clips,
                       RigidSkinRefineStats* stats = nullptr,
                       std::string* err = nullptr);

// Parse a bare .gltf JSON file into a PODModel (same outputs as the GLB
// importer). External resources are resolved relative to the .gltf file:
// buffer 0 URIs (data: base64 or a .bin file) and image URIs (data: base64
// or image files). PBR info fills exactly as in gltf_import_glb.
bool gltf_import_gltf(const std::string& path,
                      PODModel& out,
                      std::vector<GLTFImageBuffer>& images,
                      std::string* err = nullptr,
                      GLTFPBRInfo* pbr = nullptr,
                      float scale = 1.0f,
                      bool rigid_skin = false);

// Extract all animation clips inside a .glb / .gltf as individual PODModel
// objects (each containing the skeleton nodes + keyframed animation streams
// formatted as Swordigo clip PODs).
bool gltf_import_all_clips(const std::string& path,
                           std::vector<std::pair<std::string, PODModel>>& out_clips,
                           std::string* err = nullptr,
                           float scale = 1.0f,
                           float target_fps = 0.0f,
                           bool rigid_skin = false);
// Summary info about an animation clip for GUI inspection and preview
struct AnimationClipSummary {
    std::string name;
    float duration = 0.0f;
    int num_frames = 0;
    float fps = 24.0f;
    std::string origin; // "In-GLB" or "motions.json"
};

// Check for companion motions.json near glb_path
std::string gltf_find_companion_motions(const std::string& glb_path);

// Inspect animations available from a GLB file and its companion motions.json (if any)
bool gltf_inspect_animations(const std::string& glb_path,
                             std::vector<AnimationClipSummary>& in_glb_clips,
                             std::vector<AnimationClipSummary>& json_clips,
                             std::string* companion_json_path = nullptr,
                             float target_fps = 24.0f);

// Import animation clips from companion motions.json (Smash Royale and Web/Three.js formats)
bool gltf_import_companion_motions(const std::string& glb_path,
                                   const std::string& motions_json_path,
                                   std::vector<std::pair<std::string, PODModel>>& out_clips,
                                   std::string* err = nullptr,
                                   float scale = 1.0f,
                                   float target_fps = 0.0f,
                                   bool rigid_skin = false);

} // namespace av
