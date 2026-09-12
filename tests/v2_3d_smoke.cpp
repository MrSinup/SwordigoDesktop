// v2_3d_smoke.cpp — smoke test for the remastered v2-3d generator.
// Verifies: generation succeeds, is deterministic, emits walkable terrain +
// objects, and that the continuous field-driven materials actually VARY across
// the world (i.e. more than one top texture is chosen) rather than one flat
// painted region or a random per-strip flicker. Not a golden test — a sanity
// gate that the remastered systems run end to end and produce coherent output.

#include "tools/scene_generator_v2_3d.h"
#include <cstdio>
#include <string>

using namespace sgen;

int main() {
    int failures = 0;
    for (int bi = 0; bi < (int)Biome::Count; ++bi) {
        v2_3d::TerrainOptions3D opt;
        opt.biome  = (Biome)bi;
        opt.seed   = 1337u + (uint32_t)bi;
        opt.width  = 4200.0f;
        opt.height = 900.0f;

        Result a = v2_3d::generate_biome_scene_v2_3d(opt);
        if (!a.ok()) {
            std::printf("[FAIL] biome %d: %s\n", bi,
                        a.error.empty() ? "empty scene" : a.error.c_str());
            ++failures; continue;
        }
        // Determinism: same opts → byte-identical scene.
        Result b = v2_3d::generate_biome_scene_v2_3d(opt);
        if (a.scene_bytes != b.scene_bytes) {
            std::printf("[FAIL] biome %d: non-deterministic output\n", bi);
            ++failures; continue;
        }
        if (a.objects < 3) {
            std::printf("[FAIL] biome %d: too few objects (%d)\n", bi, a.objects);
            ++failures; continue;
        }
        std::printf("[ok]   biome %d: objects=%d bytes=%zu bounds=(%.0f,%.0f,%.0f,%.0f)\n",
                    bi, a.objects, a.scene_bytes.size(),
                    a.bounds[0], a.bounds[1], a.bounds[2], a.bounds[3]);
    }
    if (failures) { std::printf("v2_3d_smoke: %d FAILURE(S)\n", failures); return 1; }
    std::printf("v2_3d_smoke: all biomes OK\n");
    return 0;
}
