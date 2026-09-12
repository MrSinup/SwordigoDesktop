#pragma once
/*
 * scene_generator_v3.h — Swordigo procedural scene generator v3 ("Ultimate").
 *
 * v3 is a brand-new, data-driven algorithm built on the v3 biome database
 * (scene_v3_db.h / scene_v3_biomes.json, embedded in C++). It fixes the four
 * documented v1/v2 inconsistencies (see v3_research_notes.md):
 *
 *   1. CORRECT MESH TEXTURING — each ground platform gets the biome's real
 *      top texture (walkable surface) AND a DIFFERENT front/cliff texture,
 *      exactly as observed in the vanilla scenes (e.g. Forest: forest_grass
 *      top / forest_ground front). Cave biomes use one enclosed rock texture.
 *   2. TRUE BIOME DIFFERENTIATION — per-biome deco palettes, platform pods,
 *      torch pod, entities, background and lighting are all pulled from the DB;
 *      terrain character differs per biome (rolling hills vs. tall fire/cave
 *      shafts vs. deep-parallax grove/ice).
 *   3. PUSHED-BACK BACKGROUND DESIGN — distant design elements are scattered at
 *      the biome's real large-negative Depth band for genuine parallax; the
 *      background plane stays at ~1.72.
 *   4. REAL CAVES — enclosed geometry (floor + mirrored ceiling), rock walls,
 *      tall vertical shafts, sparse wall torches and a floor water pool.
 *
 * All .scene protobuf encoding reuses the existing sgen:: builders, so output
 * stays byte-exact with the engine's own encoding.
 */
#include "tools/scene_generator.h"

namespace sgen {
namespace v3 {

// Build a full v3 procedural level as a complete .scene (root object bytes +
// bounds), using the embedded v3 biome database. Deterministic from opt.seed.
Result generate_biome_scene_v3(const TerrainOptions& opt);

} // namespace v3
} // namespace sgen
