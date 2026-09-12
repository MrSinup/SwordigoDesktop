// scene_collision_mesh.cpp — transparent-PVR asset helper.
//
// The old auto-slicer (Z-slice → convex hull → RDP → boulder binary) was
// removed when the RubyMesh Workspace replaced it; see rubymesh_editor.cpp for
// the manual zone authoring flow. This file now only provides
// ensure_transparent_pvr(), which the Apply pipeline calls so generated
// collision objects render with a fully transparent texture in-game.
//
// No external dependencies. Pure C++17.

#include "tools/scene_collision_mesh.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

namespace col_mesh {

namespace {

// Hardcoded 116-byte PVR v2 RGBA8888 4×4 all-transparent pixel data.
// Header fields verified against spark_particle_2x.pvr from Swordigo assets:
//   Flags = 0x00018012  (HasAlpha=1, Format=0x12=RGBA8888)
//   BPP   = 32
//   Masks : R=0x000000FF G=0x0000FF00 B=0x00FF0000 A=0xFF000000
//   Magic : 0x21525650 ("PVR!")
// Structure: 52-byte header + 64-byte pixel data (4×4 × 4 bytes/pixel).
// Total: 52 + 64 = 116 bytes.
const uint8_t k_transparent_pvr[116] = {
    // ── Header (52 bytes, little-endian) ──────────────────────────────────
    0x34, 0x00, 0x00, 0x00,  // HeaderSize   = 52
    0x04, 0x00, 0x00, 0x00,  // Height       = 4
    0x04, 0x00, 0x00, 0x00,  // Width        = 4
    0x00, 0x00, 0x00, 0x00,  // MIPMapCount  = 0  (1 level)
    0x12, 0x80, 0x01, 0x00,  // Flags        = 0x00018012 (HasAlpha | RGBA8888)
    0x40, 0x00, 0x00, 0x00,  // DataLength   = 64 (4*4*4)
    0x20, 0x00, 0x00, 0x00,  // BPP          = 32
    0xFF, 0x00, 0x00, 0x00,  // RedMask      = 0x000000FF
    0x00, 0xFF, 0x00, 0x00,  // GreenMask    = 0x0000FF00
    0x00, 0x00, 0xFF, 0x00,  // BlueMask     = 0x00FF0000
    0x00, 0x00, 0x00, 0xFF,  // AlphaMask    = 0xFF000000
    0x50, 0x56, 0x52, 0x21,  // Magic        = "PVR!" = 0x21525650
    0x01, 0x00, 0x00, 0x00,  // NumSurfaces  = 1
    // ── Pixel data (64 bytes = 4×4 pixels × 4 bytes RGBA, all zero) ──────
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
};
static_assert(sizeof(k_transparent_pvr) == 116,
              "PVR literal must be 52-byte header + 64-byte pixels = 116 bytes");

} // namespace

std::string ensure_transparent_pvr(const std::string& scene_dir) {
    if (scene_dir.empty()) return "";

    const std::string target = scene_dir + "/ruby_transparent.pvr";

    // Already exists — fast path.
    {
        std::ifstream probe(target, std::ios::binary);
        if (probe.good()) return target;
    }

    // Write via temp file for atomic-ish replace (avoids half-written files if
    // the process is killed mid-write).
    const std::string tmp = target + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return "";
        f.write(reinterpret_cast<const char*>(k_transparent_pvr),
                sizeof(k_transparent_pvr));
        if (!f) return "";
    }

    // Rename tmp → target (POSIX: atomic if same filesystem, which it is).
    if (std::rename(tmp.c_str(), target.c_str()) != 0) {
        std::remove(tmp.c_str());
        return "";
    }
    return target;
}

} // namespace col_mesh
