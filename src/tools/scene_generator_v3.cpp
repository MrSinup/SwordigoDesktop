/*
 * scene_generator_v3.cpp — Swordigo procedural scene generator v3 ("Ultimate").
 *
 * v3 IS the proven v2 pipeline (identical terrain synthesis, Z-layer stack,
 * seam-shared platform slicing, vanilla depth tiers, spawn plateau, walkable-
 * band Bounds), but driven by the embedded v3 biome database (scene_v3_db.*)
 * instead of the compiled BiomeSpecV2 table — so every biome gets its REAL
 * vanilla top-vs-front mesh textures, deco/torch/entity palettes, background
 * and lighting. On top of v2 it adds real ENCLOSED caves (mirrored rock ceiling
 * with vertical shafts) for cave biomes.
 *
 * All .scene protobuf encoding reuses the existing sgen:: builders, so output
 * stays byte-exact with the engine's own encoding — and geometry matches v2.
 */
#include "tools/scene_generator_v3.h"
#include "tools/scene_generator.h"
#include "tools/scene_v3_db.h"
#include "platform/protobuf_reader.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace sgen {
namespace v3 {

// ── Local helpers (copied verbatim from scene_generator_v2.cpp) ─────────────

static std::string local_extract_object_bytes(const std::string& scene) {
    if (scene.empty() || (unsigned char)scene[0] != 0x0Au) return "";
    size_t i = 1;
    uint64_t len = 0; int sh = 0;
    while (i < scene.size() && ((unsigned char)scene[i] & 0x80)) {
        len |= (uint64_t)(scene[i] & 0x7fu) << sh; sh += 7; ++i;
    }
    if (i >= scene.size()) return "";
    len |= (uint64_t)(unsigned char)scene[i] << sh; ++i;
    if (i + len > scene.size()) return "";
    return scene.substr(i, (size_t)len);
}

static void local_track_aabb(float& minx, float& miny, float& maxx, float& maxy,
                             const std::vector<Vec2>& poly) {
    for (const auto& v : poly) {
        minx = std::min(minx, v.x); miny = std::min(miny, v.y);
        maxx = std::max(maxx, v.x); maxy = std::max(maxy, v.y);
    }
}

static inline uint32_t local_hash_uint(uint32_t x) {
    x = (x ^ 61u) ^ (x >> 16);
    x = x + (x << 3);
    x = x ^ (x >> 4);
    x = x * 0x27d4eb2du;
    x = x ^ (x >> 15);
    return x;
}

static void local_smooth_gaussian(std::vector<float>& h, int radius = 4, int passes = 3) {
    const int n = (int)h.size();
    if (n == 0) return;
    std::vector<float> tmp(n);
    for (int pass = 0; pass < passes; ++pass) {
        const float sigma = radius * 0.45f;
        float wsum = 0.0f;
        std::vector<float> w(2 * radius + 1);
        for (int k = -radius; k <= radius; ++k) {
            w[k + radius] = std::exp(-0.5f * (k * k) / (sigma * sigma));
            wsum += w[k + radius];
        }
        for (auto& ww : w) ww /= wsum;
        for (int i = 0; i < n; ++i) {
            float v = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int j = std::clamp(i + k, 0, n - 1);
                v += w[k + radius] * h[j];
            }
            tmp[i] = v;
        }
        h = tmp;
    }
}

static void apply_terracing(std::vector<float>& h, float strength, uint32_t /*seed*/) {
    if (h.empty() || strength <= 0.0f) return;
    const float mn = *std::min_element(h.begin(), h.end());
    const float mx = *std::max_element(h.begin(), h.end());
    const float range = mx - mn;
    if (range < 10.0f) return;
    const int steps = 6;
    const float inv_range = 1.0f / range;
    for (auto& v : h) {
        float norm = (v - mn) * inv_range;
        float terr = std::floor(norm * steps + 0.5f) / steps;
        v = mn + (norm + (terr - norm) * strength) * range;
    }
}

struct TopEdge { std::vector<Vec2> pts; float min_x = 0, max_x = 0; };

static TopEdge build_top_edge(const Platform& plat) {
    std::vector<Vec2> poly = plat.polygon;
    if (poly.size() < 3)
        poly = make_rect_polygon(plat.rect[0], plat.rect[1], plat.rect[2], plat.rect[3]);
    const float bucket = 24.0f;
    float mn = 1e9f, mx = -1e9f;
    for (const auto& v : poly) { mn = std::min(mn, v.x); mx = std::max(mx, v.x); }
    const int nb = std::max(1, (int)((mx - mn) / bucket));
    std::vector<float> top(nb, -1e9f);
    for (const auto& v : poly) {
        const int bk = std::clamp((int)((v.x - mn) / bucket), 0, nb - 1);
        top[bk] = std::max(top[bk], v.y);
    }
    TopEdge te; te.min_x = mn; te.max_x = mx;
    for (int b = 0; b < nb; ++b)
        if (top[b] > -1e8f) te.pts.push_back({mn + (b + 0.5f) * bucket, top[b]});
    return te;
}

static float ground_y_at(const TopEdge& te, float x) {
    if (te.pts.empty()) return -1e9f;
    if (x <= te.pts.front().x) return te.pts.front().y;
    if (x >= te.pts.back().x)  return te.pts.back().y;
    for (size_t k = 1; k < te.pts.size(); ++k) {
        if (x <= te.pts[k].x) {
            const float t = (x - te.pts[k-1].x) / std::max(1e-3f, te.pts[k].x - te.pts[k-1].x);
            return te.pts[k-1].y + t * (te.pts[k].y - te.pts[k-1].y);
        }
    }
    return te.pts.back().y;
}

// Deterministic pick from a DB string list (empty-safe).
static std::string pick(const std::vector<std::string>& v, uint32_t idx,
                        const char* fallback) {
    if (v.empty()) return fallback ? fallback : "";
    return v[idx % v.size()];
}

// ── Decoration classification (for realistic, size-aware placement) ─────────
// A pod is a "rock-like" static prop (rock/stone/pillar/pebble/debris/pot) as
// opposed to a tree/plant/bush. Used to split the play palette into a tree pass
// (planted upright on the plane) and a rock pass (size-graded scree clusters).
static bool is_rock_pod(const std::string& s) {
    static const char* kRock[] = {
        "rock", "stone", "pillar", "pillair", "boulder", "pebble", "rubble",
        "pot", "debris", "brick", "tombstone", "stalag"
    };
    for (const char* k : kRock)
        if (s.find(k) != std::string::npos) return true;
    return false;
}

// Rough 0..1 size class for a rock pod name (bigger name → larger scale).
// "huge" > "big"/"large"/"stonepile"/"pillar" > default > "small"/"pebble".
static float rock_size_class(const std::string& s) {
    if (s.find("huge") != std::string::npos || s.find("giant") != std::string::npos) return 1.0f;
    if (s.find("small") != std::string::npos || s.find("pebble") != std::string::npos) return 0.15f;
    if (s.find("pile") != std::string::npos || s.find("pillar") != std::string::npos ||
        s.find("pillair") != std::string::npos || s.find("big") != std::string::npos ||
        s.find("large") != std::string::npos || s.find("boulder") != std::string::npos)
        return 0.8f;
    // A bare "rock1"/"rock2" is mid-size; "rock" with a trailing digit stays mid.
    if (s.find("rock") != std::string::npos) return 0.5f;
    return 0.4f;
}

// Choose the smallest rock in the set for satellite pebbles (falls back to the
// anchor if nothing smaller exists).
static std::string smallest_rock_pod(const std::vector<std::string>& rocks,
                                     const std::string& anchor) {
    std::string best = anchor;
    float best_sz = rock_size_class(anchor);
    for (const auto& s : rocks) {
        const float sz = rock_size_class(s);
        if (sz < best_sz) { best_sz = sz; best = s; }
    }
    return best;
}

// ════════════════════════════════════════════════════════════════════════════
// ADVANCED PROCEDURAL CAVE GENERATOR (from scratch)
// ════════════════════════════════════════════════════════════════════════════
//
// Real vanilla caves (thecave_*, florennum_cave1, forest_cave1, wasteland_cave)
// are enclosed rock chambers: an undulating FLOOR of brown128/cavewalls ground
// meshes, a rough cavewalls CEILING above, interior LEDGE platforms at varied
// heights for climbing, torches + point lights for glow, and a dark
// cavesbackground2. They are NOT open strips and never use ice/grass textures.
//
// Math model — a winding tunnel carved through solid rock:
//   center(x) = A1*sin(w1*x + p1) + A2*fbm(x)        (tunnel vertical center)
//   half(x)   = H0 + Hn*|fbm2(x)|                     (tunnel half-height)
//   floor(x)  = center(x) - half(x)                   (walkable floor top)
//   ceil(x)   = center(x) + half(x)                   (ceiling underside)
// The floor and ceiling are each baked as GroundMesh strips that hug those
// curves; solid rock fills below the floor and above the ceiling. Interior
// ledges are short GroundMesh shelves floated in the void between floor and
// ceiling, spaced by a third noise channel so they form a climbable route.
static Result generate_cave_v3(const TerrainOptions& opt, const V3BiomeSpec& bio) {
    Result r;
    const uint32_t seed  = opt.seed ? opt.seed : 1u;
    const float width    = std::max(1200.0f, opt.width);
    const float half_w   = width * 0.5f;
    const float kDepth   = 112.0f;
    uint64_t drng = (uint64_t)seed ^ 0xCA7E1337ull;

    // Cave textures (real): floor = brown128 top / cavewalls face; walls &
    // ceiling = cavewalls (+ cavewalls2 variation). Never grass/ice.
    const std::string floor_top   = bio.tex_top.empty()   ? "brown128"  : bio.tex_top;   // brown128
    const std::string rock_face   = bio.tex_front.empty() ? "cavewalls" : bio.tex_front; // cavewalls
    const std::string wall_tex    = bio.tex_wall.empty()  ? "cavewalls" : bio.tex_wall;

    // Tunnel shape parameters (world units). Y is negative-down (v2 convention),
    // but the cave is authored in its own local Y then emitted directly.
    const float A1 = 220.0f, A2 = 260.0f;      // center-line amplitudes
    const float w1 = 6.2831853f / (width * 0.42f);
    const float p1 = rng_float(drng, 0.0f, 6.2831853f);
    const float H0 = 200.0f, Hn = 190.0f;      // tunnel half-height base + noise

    auto center_at = [&](float x) -> float {
        const float nx = (x + half_w) / width;
        return A1 * std::sin(w1 * x + p1)
             + A2 * fbm_1d(nx * 3.0f, opt.octaves, seed ^ 0x1111u, 0.4f);
    };
    auto half_at = [&](float x) -> float {
        const float nx = (x + half_w) / width;
        const float n  = std::fabs(fbm_1d(nx * 4.5f, std::max(3, opt.octaves), seed ^ 0x2222u, 0.3f));
        return H0 + Hn * n;                    // 200..390 tall corridor
    };

    proto::Writer scene;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    r.objects = 0;

    auto emitw = [&](const std::string& wrapped) {
        if (wrapped.empty()) return;
        std::string bare = local_extract_object_bytes(wrapped);
        if (bare.empty()) return;
        scene.write_bytes_field(1, bare); ++r.objects;
    };

    // Dark cave background.
    emitw(build_background_object(bio.background));
    // Dim directional light (caves rely on point lights/torches).
    emitw(build_light_object("DirectionalLight", 0.0f, 0.0f, 137.0f,
                             std::clamp(bio.key_intensity, 0.5f, 2.2f),
                             std::clamp(bio.ambient_intensity, 0.05f, 0.4f),
                             0.4f, bio.key_color[0], bio.key_color[1], bio.key_color[2]));

    // Sample the tunnel across X.
    const int   samples = std::max(64, (int)(width / 45.0f));
    const float step    = width / (float)(samples - 1);
    std::vector<float> fl(samples), ce(samples);
    for (int i = 0; i < samples; ++i) {
        const float x = -half_w + i * step;
        const float c = center_at(x), h = half_at(x);
        fl[i] = c - h;   // floor top Y
        ce[i] = c + h;   // ceiling underside Y
    }

    // Slice into strips sharing seams (v2-style llround) and bake FLOOR + CEIL.
    const int n_strips = std::clamp(opt.platform_count * 2, 6, 26);
    const float rock_thick = 260.0f;   // how far solid rock extends past the void

    for (int p = 0; p < n_strips; ++p) {
        const int s0 = std::clamp((int)std::llround((double)p       * (samples - 1) / n_strips), 0, samples - 1);
        const int s1 = std::clamp((int)std::llround((double)(p + 1) * (samples - 1) / n_strips), 0, samples - 1);
        if (s1 <= s0) continue;
        const int cnt = s1 - s0 + 1;
        const float x0 = -half_w + s0 * step;

        // FLOOR strip: top edge follows fl[], base extends down into rock.
        {
            std::vector<float> h(cnt);
            float base = 1e9f;
            for (int i = 0; i < cnt; ++i) { h[i] = fl[s0 + i]; base = std::min(base, h[i]); }
            base -= rock_thick;
            auto poly = make_heightfield_polygon(h.data(), cnt, step, base);
            for (auto& v : poly) v.x += x0;
            Platform plat;
            plat.polygon = poly; plat.horiz_noise = 0.0f;
            plat.seed = (uint32_t)(seed ^ ((uint32_t)p * 0x9E37u));
            plat.top_texture = floor_top; plat.front_texture = rock_face;
            plat.z = 0.0f; plat.min_depth = -kDepth; plat.max_depth = kDepth;
            plat.surface_width = 120.0f;
            emitw(build_ground_object(plat, "cave_floor_" + std::to_string(p)));
            local_track_aabb(minx, miny, maxx, maxy, poly);
        }

        // CEILING strip: it is solid rock ABOVE ce[]. Build a downward-hanging
        // mass: its underside follows ce[], its top extends up into rock. We do
        // this by making a polygon whose "surface" is the ceiling underside but
        // flipped so the filled side is up.
        {
            std::vector<float> h(cnt);
            float top = -1e9f;
            for (int i = 0; i < cnt; ++i) { h[i] = ce[s0 + i]; top = std::max(top, h[i]); }
            // Build a normal heightfield for the underside, then lift the base
            // far above so the solid fill sits overhead (inverted winding).
            auto poly = make_heightfield_polygon(h.data(), cnt, step, top + rock_thick);
            // Flip vertically around the ceiling line so rock is on top.
            for (auto& v : poly) v.y = (2.0f * ce[s0]) - v.y;
            std::reverse(poly.begin(), poly.end());
            for (auto& v : poly) v.x += x0;
            Platform plat;
            plat.polygon = poly; plat.horiz_noise = 0.0f;
            plat.seed = (uint32_t)(seed ^ ((uint32_t)p * 0x51EDu) ^ 0xC0FFEEu);
            plat.top_texture = wall_tex; plat.front_texture = wall_tex;
            plat.z = 0.0f; plat.min_depth = -kDepth; plat.max_depth = kDepth;
            plat.surface_width = 120.0f;
            emitw(build_ground_object(plat, "cave_ceil_" + std::to_string(p)));
            local_track_aabb(minx, miny, maxx, maxy, poly);
        }
    }

    // ── Interior climbable LEDGES (advanced): float short rock shelves in the
    //    void between floor and ceiling, following a noise route so they form a
    //    climbable path. Each ledge is a small flat GroundMesh (brown128 top).
    {
        const int   n_ledge = std::clamp((int)(width / 520.0f) + opt.platform_count, 4, 22);
        for (int k = 0; k < n_ledge; ++k) {
            const float lx = -half_w + rng_float(drng, 0.10f, 0.90f) * width;
            const float c  = center_at(lx), h = half_at(lx);
            const float fl_y = c - h, ce_y = c + h;
            if (ce_y - fl_y < 220.0f) continue;               // too tight, skip
            // Route the ledge height by a noise channel so successive ledges
            // step upward/downward (climbable), staying inside the void margin.
            const float t = 0.5f + 0.42f * fbm_1d((lx + half_w) / width * 5.0f, 3, seed ^ 0x3333u, 0.0f);
            const float ly = fl_y + std::clamp(t, 0.12f, 0.88f) * (ce_y - fl_y);
            const float lw = rng_float(drng, 150.0f, 320.0f);
            const float lh = 30.0f + rng_float(drng, 0.0f, 20.0f);
            auto poly = make_rect_polygon(lx, ly, lw, lh);
            Platform led;
            led.polygon = poly; led.horiz_noise = 0.0f;
            led.seed = (uint32_t)(seed ^ ((uint32_t)k * 0x2545F4u));
            led.top_texture = floor_top; led.front_texture = rock_face;
            led.z = 0.0f; led.min_depth = -kDepth; led.max_depth = kDepth;
            led.surface_width = 90.0f;
            emitw(build_ground_object(led, "cave_ledge_" + std::to_string(k)));
            local_track_aabb(minx, miny, maxx, maxy, poly);

            // Torch or point light on the ledge for glow.
            if ((k % 2) == 0) {
                TorchLight tl;
                tl.x = lx; tl.y = ly + lh * 0.5f + 8.0f; tl.z = rng_float(drng, -20.0f, 30.0f);
                tl.radius = bio.torch_radius; tl.intensity = bio.torch_intensity;
                tl.glow_r = std::min(0.49f, bio.torch_glow[0]);  // plain "torch" pod
                tl.glow_g = bio.torch_glow[1]; tl.glow_b = bio.torch_glow[2];
                emitw(build_torch_object(tl, "cave_torch_" + std::to_string(k)));
            } else {
                // Soft point glow (like vanilla point_* lights).
                TorchLight gl;
                gl.x = lx + rng_float(drng, -30.0f, 30.0f);
                gl.y = ly + rng_float(drng, 20.0f, 90.0f);
                gl.z = rng_float(drng, -30.0f, 20.0f);
                gl.radius = 150.0f + rng_float(drng, 0.0f, 120.0f);
                gl.intensity = 1.4f; gl.glow_a = 0.4f;
                gl.glow_r = bio.torch_glow[0]; gl.glow_g = bio.torch_glow[1]; gl.glow_b = bio.torch_glow[2];
                emitw(build_glow_light(gl, "cave_glow_" + std::to_string(k)));
            }
        }
    }

    // ── Floor torches (walk-path lighting) + sparse rock/pot decos on floor ─
    {
        const float tspace = (opt.torch_spacing > 0.0f) ? opt.torch_spacing : 360.0f;
        int ti = 0;
        for (float x = -half_w + tspace * 0.5f; x < half_w; x += tspace) {
            const float fy = center_at(x) - half_at(x);
            TorchLight tl;
            tl.x = x; tl.y = fy + 8.0f; tl.z = rng_float(drng, -10.0f, 20.0f);
            tl.radius = bio.torch_radius; tl.intensity = bio.torch_intensity;
            tl.glow_r = std::min(0.49f, bio.torch_glow[0]);
            tl.glow_g = bio.torch_glow[1]; tl.glow_b = bio.torch_glow[2];
            emitw(build_torch_object(tl, "cave_ftorch_" + std::to_string(++ti)));
        }
        // Sparse floor decorations (rocks / pot) — real caves are sparse.
        if (!bio.deco_play.empty()) {
            const float dens = std::clamp(opt.deco_density, 0.0f, 2.0f);
            const float sp = std::clamp(320.0f / (dens + 1e-3f), 260.0f, 900.0f);
            int di = 0;
            for (float x = -half_w + rng_float(drng, 0.0f, sp); x < half_w; x += sp * rng_float(drng, 0.7f, 1.4f)) {
                Deco d;
                d.x = x; d.y = center_at(x) - half_at(x) - 1.0f;
                d.z = rng_float(drng, -6.0f, 8.0f);
                d.scale = opt.randomize_deco_scale ? (0.55f + rng_float(drng, 0.0f, 0.5f)) : 1.0f;
                d.pod = pick(bio.deco_play, local_hash_uint((uint32_t)(di * 137 + 11)), "rock1");
                emitw(build_deco_object(d, "cave_deco_" + std::to_string(++di)));
            }
        }
    }

    // ── Water pool at the lowest floor point (vanilla caves have pools) ────
    if (opt.add_water && bio.water_enabled && bio.water_front[3] > 0.01f) {
        float lowest = 1e9f, at = 0.0f;
        for (int i = 0; i < samples; ++i) if (fl[i] < lowest) { lowest = fl[i]; at = -half_w + i * step; }
        Water w;
        w.rect[0] = at; w.rect[1] = lowest - 20.0f; w.rect[2] = 700.0f; w.rect[3] = 46.0f;
        memcpy(w.front_rgba, bio.water_front, sizeof(float) * 4);
        memcpy(w.surface_rgba, bio.water_surface, sizeof(float) * 4);
        emitw(build_water_object(w, "cave_water"));
    }

    // ── Spawn on the floor near the middle. ────────────────────────────────
    {
        const float sxp = 0.0f;
        const float sy = center_at(sxp) - half_at(sxp) + 56.0f;
        emitw(build_spawn_object("spawn_default", sxp, sy, 1));
    }

    // ── ObjectLibrary ──────────────────────────────────────────────────────
    static const std::vector<std::string> kImports = {
        "caves_stuff", "collectibles", "florennum_stuff", "forest",
        "game_common", "grovestuff", "lights", "monsters", "npc",
        "plains_stuff", "platforms", "playground", "programs", "rocks",
        "scriptarea", "traps_stuffs", "trash", "woodkeep_stuff", "woods",
    };
    scene.write_bytes_field(2, build_object_library(opt.scene_name, kImports));

    if (maxx <= minx || maxy <= miny) { r.error = "v3-cave: empty AABB"; return r; }
    const float pad = 240.0f;
    const float bx = minx - pad, by = miny - pad;
    const float bw = (maxx - minx) + 2.0f * pad, bh = (maxy - miny) + 2.0f * pad;
    scene.write_bytes_field(3, build_bounds_payload(bx, by, bw, bh));
    r.bounds[0] = bx; r.bounds[1] = by; r.bounds[2] = bw; r.bounds[3] = bh;
    r.scene_bytes = scene.to_string();
    return r;
}

// ── Main entry — v2 algorithm, v3 database ──────────────────────────────────
Result generate_biome_scene_v3(const TerrainOptions& opt) {
    Result r;

    const std::vector<V3BiomeSpec>& db = v3_biome_db();
    if (db.empty()) { r.error = "v3: biome DB failed to parse"; return r; }
    const V3BiomeSpec& bio = v3_biome((int)opt.biome);

    // Caves use the dedicated advanced cave generator.
    if (bio.cave) return generate_cave_v3(opt, bio);

    const uint32_t seed     = opt.seed ? opt.seed : 1u;
    const int    n_plat     = std::max(2, opt.platform_count);
    const float  half_w     = opt.width * 0.5f;
    const float  floor_y    = -opt.height * 0.5f;   // NEGATIVE = downward (v2 parity)
    const float  kGroundDepth = 112.0f;
    const bool   is_cave    = bio.cave;

    // Enclosed-cave ceiling: mirror the walkable ground above with a headroom
    // gap so the player has a corridor. Cave meshes use one rock texture.
    const float  kCaveGap   = 340.0f;

    // ── Z-layer stack (identical structure to v2) ─────────────────────────
    struct LayerDesc {
        float z;
        float scale_x;
        float height_scale;
        bool  walkable;
        bool  emit_decos;
        int   smooth_passes;
        float roughness_mult;
        std::string top_tex;
        std::string front_tex;
    };
    std::vector<LayerDesc> layers;

    // Background visual layers at vanilla-confirmed depths (~-130 / -165).
    const int bg_layers = is_cave ? 1 : 2;
    {
        const float bg_z0 = -130.0f;
        const float bg_z1 = -165.0f;
        for (int i = 0; i < bg_layers; ++i) {
            LayerDesc bg;
            bg.z             = (i == 0) ? bg_z0 : bg_z1 - (float)(i - 1) * 30.0f;
            bg.scale_x       = 1.2f + (float)i * 0.2f;
            bg.height_scale  = 1.8f + (float)i * 0.35f;
            bg.walkable      = false;
            bg.emit_decos    = true;
            bg.smooth_passes = std::max(1, 2 - i);
            bg.roughness_mult = 1.8f + (float)i * 0.4f;
            // Background terrain uses the biome bottom/side rock tone.
            bg.top_tex       = bio.tex_bottom.empty() ? bio.tex_front : bio.tex_bottom;
            bg.front_tex     = bg.top_tex;
            layers.push_back(bg);
        }
    }

    // Main walkable layer (Z ≈ 0) — CORRECT top vs front textures from DB.
    {
        LayerDesc main;
        main.z            = 0.0f;
        main.scale_x      = 1.0f;
        main.height_scale = 1.0f;
        main.walkable     = true;
        main.emit_decos   = true;
        main.smooth_passes = 3;
        main.roughness_mult = 1.0f;
        main.top_tex      = bio.tex_top;
        // Cave: enclosed rock — top and front share the same face.
        main.front_tex    = (is_cave && bio.cave_top_equals_front) ? bio.tex_top : bio.tex_front;
        layers.push_back(main);
    }

    // ── Shared scene writer ───────────────────────────────────────────────
    proto::Writer scene;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    float top_min = 1e30f, top_max = -1e30f;
    uint64_t drng = (uint64_t)seed ^ 0xBEEF1337ull;
    r.objects = 0;

    // Background + light emitted once (from DB).
    scene.write_bytes_field(1, local_extract_object_bytes(
        build_background_object(bio.background)));
    ++r.objects;

    const float light_depth = 17.0f + opt.width * 0.018f;
    scene.write_bytes_field(1, local_extract_object_bytes(
        build_light_object("DirectionalLight", 0.0f, 0.0f, light_depth,
                           std::clamp(bio.key_intensity, 0.5f, 3.4f),
                           std::clamp(bio.ambient_intensity, 0.05f, 1.0f),
                           0.4f,
                           bio.key_color[0], bio.key_color[1], bio.key_color[2])));
    ++r.objects;

    static const float kPlateauHalf = 0.12f;
    std::vector<Platform> walkable_plats;

    for (const auto& layer : layers) {
        // 1. Heightfield synthesis (v2 amplitude: opt.height * 0.28f).
        const float layer_width = opt.width * layer.scale_x;
        const int samples = std::max(48, (int)(layer_width / 40.0f));
        const float step  = layer_width / (float)(samples - 1);

        std::vector<float> prof(samples);
        for (int i = 0; i < samples; ++i) {
            const float x  = -layer_width * 0.5f + (float)i * step;
            const float nx = x / layer_width * 6.0f;
            float v;
            if (opt.mountains) {
                const float ridge = ridged_2d(nx, 0.5f, opt.octaves, seed ^ (uint32_t)(int)layer.z);
                const float low   = fbm_1d(nx, std::max(2, opt.octaves - 1), seed ^ 0xFFu ^ (uint32_t)(int)layer.z, 0.2f);
                v = ridge * 0.70f + low * 0.30f;
            } else {
                v = fbm_1d(nx, opt.octaves, seed ^ (uint32_t)(int)layer.z, 0.35f);
            }
            // Cave floor is a little rougher for shaft-like relief.
            const float amp = opt.height * (is_cave && layer.walkable ? 0.34f : 0.28f);
            prof[i] = v * (amp * opt.roughness * layer.roughness_mult * layer.height_scale);
        }

        // 2. Smoothing.
        local_smooth_gaussian(prof, 5, layer.smooth_passes);

        // 3. Spawn-pad guarantee (main walkable layer only).
        if (layer.walkable) {
            const float plateau_half = layer_width * kPlateauHalf;
            const float base         = opt.height * 0.02f;
            for (int i = 0; i < samples; ++i) {
                const float x = -layer_width * 0.5f + (float)i * step;
                const float d = std::fabs(x) / plateau_half;
                if (d >= 1.0f) continue;
                const float plat_h = base + (1.0f - d * d) * opt.height * 0.04f;
                prof[i] = std::max(prof[i], plat_h);
            }
        }

        // 5. Platform slicing (seam-shared, llround boundaries — v2 parity).
        const int n_strips = layer.walkable ? n_plat : std::max(1, n_plat / 2);

        for (int p = 0; p < n_strips; ++p) {
            const int s0 = std::clamp((int)std::llround((double)p       * (samples - 1) / n_strips), 0, samples - 1);
            const int s1 = std::clamp((int)std::llround((double)(p + 1) * (samples - 1) / n_strips), 0, samples - 1);
            if (s1 <= s0) continue;
            const int cnt = s1 - s0 + 1;

            std::vector<float> h(cnt);
            for (int i = 0; i < cnt; ++i) h[i] = prof[s0 + i];

            const float x0 = -layer_width * 0.5f + (float)s0 * step;

            auto poly = make_heightfield_polygon(h.data(), cnt, step,
                                                 floor_y * layer.height_scale - opt.height * 0.05f);
            for (auto& v : poly) v.x += x0;

            Platform plat;
            plat.polygon       = poly;
            plat.seed          = (uint32_t)(seed ^ ((uint32_t)p * 0x9E37u) ^ (uint32_t)(int)layer.z);
            plat.horiz_noise   = 0.0f;
            plat.top_texture   = layer.top_tex;
            plat.front_texture = layer.front_tex;
            plat.z             = layer.z;
            plat.min_depth     = -kGroundDepth;
            plat.max_depth     =  kGroundDepth;
            plat.surface_width = 100.0f + (float)p * 15.0f;

            const std::string nm = "ground_L" + std::to_string((int)layer.z) + "_" + std::to_string(p);
            const std::string bytes = build_ground_object(plat, nm);
            if (bytes.empty()) continue;
            scene.write_bytes_field(1, local_extract_object_bytes(bytes));
            ++r.objects;

            if (layer.walkable) {
                local_track_aabb(minx, miny, maxx, maxy, poly);
                walkable_plats.push_back(plat);
                const TopEdge te2 = build_top_edge(plat);
                for (const auto& pt : te2.pts) {
                    top_min = std::min(top_min, pt.y);
                    top_max = std::max(top_max, pt.y);
                }

                // ── Enclosed cave ceiling: mirror this strip above with a gap.
                if (is_cave) {
                    Platform ceil;
                    ceil.polygon = poly;
                    for (auto& v : ceil.polygon) v.y = -v.y + (2.0f * top_max + kCaveGap);
                    std::reverse(ceil.polygon.begin(), ceil.polygon.end());
                    ceil.seed = plat.seed ^ 0x5bd1e995u;
                    ceil.horiz_noise = 0.0f;
                    ceil.top_texture   = bio.tex_wall.empty() ? bio.tex_top : bio.tex_wall;
                    ceil.front_texture = ceil.top_texture;
                    ceil.z = 0.0f;
                    ceil.min_depth = -kGroundDepth; ceil.max_depth = kGroundDepth;
                    ceil.surface_width = plat.surface_width;
                    const std::string cb = build_ground_object(
                        ceil, "cave_ceil_" + std::to_string(p));
                    if (!cb.empty()) {
                        scene.write_bytes_field(1, local_extract_object_bytes(cb));
                        ++r.objects;
                        local_track_aabb(minx, miny, maxx, maxy, ceil.polygon);
                    }
                }
            }

            // ── Decoration scatter (v2 tiers + logic; DB palettes) ─────────
            if (!layer.emit_decos) continue;

            const TopEdge te = build_top_edge(plat);
            if (te.pts.size() < 2) continue;
            const float dens = std::clamp(opt.deco_density, 0.0f, 2.0f);

            auto scatter_pass = [&](float mean_sp, uint32_t salt,
                                    const std::function<void(float, float)>& emitfn) {
                if (mean_sp < 24.0f) mean_sp = 24.0f;
                uint64_t s = ((uint64_t)seed << 1) ^ (uint64_t)salt
                           ^ ((uint64_t)p * 0x9E37u)
                           ^ ((uint64_t)(uint32_t)(int)layer.z * 0xDEADBEEFu);
                float x = te.min_x + rng_float(s, 0.0f, mean_sp);
                while (x < te.max_x) {
                    const float gy = ground_y_at(te, x);
                    if (gy > -1e8f) emitfn(x, gy);
                    x += mean_sp * rng_float(s, 0.60f, 1.40f);
                }
            };
            auto biased_depth = [&](float near_lo, float near_hi,
                                    float far_lo, float far_hi) -> float {
                return (rng_float(drng, 0.0f, 1.0f) < 0.65f)
                    ? rng_float(drng, near_lo, near_hi)
                    : rng_float(drng, far_lo, far_hi);
            };

            int deco_idx = 0, torch_idx = 0, glow_idx = 0;

            if (layer.walkable) {
                // ── Split the play palette into TREES/PLANTS vs ROCKS so each
                //    gets realistic, size-aware placement. Trees/bushes sit ON
                //    the walkable plane (z≈0) so they stand on the platform;
                //    rocks cluster by size with big anchors + small satellites.
                std::vector<std::string> trees, rocks, props;
                for (const auto& nm : bio.deco_play) {
                    // Pots/breakables are their own prop class (placed upright on
                    // the ground at a steady rate), rocks are scree, rest = trees.
                    if (nm == "pot" || nm.find("pot") != std::string::npos ||
                        nm.find("vase") != std::string::npos)
                        props.push_back(nm);
                    else if (is_rock_pod(nm)) rocks.push_back(nm);
                    else trees.push_back(nm);
                }

                // Trees / plants / bushes — planted ON the platform surface.
                if (!trees.empty()) {
                    const float sp = std::clamp(170.0f / (dens + 1e-3f), 140.0f, 820.0f);
                    scatter_pass(sp, 0xA11CEu, [&](float x, float gy) {
                        // Small natural clumps: 1 main + occasional saplings.
                        const int cluster = (rng_float(drng, 0.0f, 1.0f) < 0.28f) ? 2 : 1;
                        for (int ci = 0; ci < cluster; ++ci) {
                            Deco d;
                            d.x = x + (ci ? rng_float(drng, 34.0f, 78.0f) : rng_float(drng, -14.0f, 14.0f));
                            d.y = gy - 2.0f;                       // rooted on the ground line
                            // ON the walkable plane; only a slight parallax jitter
                            // so trunks never float in front of / behind the floor.
                            d.z = rng_float(drng, -6.0f, 8.0f);
                            // Saplings in a clump are smaller than the main tree.
                            d.scale = opt.randomize_deco_scale
                                ? (ci ? 0.60f + rng_float(drng, 0.0f, 0.30f)
                                      : 0.90f + rng_float(drng, 0.0f, 0.55f))
                                : 1.0f;
                            d.rot_y = (opt.randomize_deco_rotation && rng_float(drng, 0.0f, 1.0f) < 0.45f) ? -1.5707963f : 0.0f;
                            d.pod = pick(trees, local_hash_uint((uint32_t)(deco_idx * 131 + 7)), "bush");
                            const std::string b = build_deco_object(d, "deco_tree" + std::to_string(++deco_idx));
                            if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
                        }
                    });
                }

                // Rocks / pots / debris — size-graded scatter, on the platform.
                // A "big" anchor rock drops first, then 0-2 smaller rocks huddle
                // around it (realistic scree distribution), each scaled to size.
                if (!rocks.empty()) {
                    const float sp = std::clamp(240.0f / (dens + 1e-3f), 190.0f, 1000.0f);
                    scatter_pass(sp, 0xB0B0u, [&](float x, float gy) {
                        // Anchor rock — larger pods (rock/hugerock/stonepile) get
                        // a bigger scale; small pods stay small.
                        const std::string anchor = pick(rocks, local_hash_uint((uint32_t)(deco_idx * 337 + 13)), "rock1");
                        const float anchor_big = rock_size_class(anchor); // 0..1 (bigger = larger)
                        {
                            Deco d;
                            d.x = x + rng_float(drng, -10.0f, 10.0f);
                            d.y = gy - 1.0f;                       // seated on the ground
                            d.z = rng_float(drng, -4.0f, 6.0f);    // on the plane
                            d.scale = opt.randomize_deco_scale
                                ? (0.55f + anchor_big * 0.9f + rng_float(drng, 0.0f, 0.35f))
                                : (0.8f + anchor_big * 0.6f);
                            d.rot_y = (opt.randomize_deco_rotation && rng_float(drng, 0.0f, 1.0f) < 0.5f) ? -1.5707963f : 0.0f;
                            d.pod = anchor;
                            const std::string b = build_deco_object(d, "deco_rock" + std::to_string(++deco_idx));
                            if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
                        }
                        // Satellite pebbles: more around a big anchor, fewer around small.
                        const int sats = (int)std::floor(rng_float(drng, 0.0f, 1.0f) + anchor_big * 1.6f);
                        for (int si = 0; si < sats; ++si) {
                            Deco d;
                            d.x = x + rng_float(drng, -46.0f, 46.0f);
                            d.y = gy - 1.0f;
                            d.z = rng_float(drng, -8.0f, 10.0f);
                            d.scale = opt.randomize_deco_scale ? (0.28f + rng_float(drng, 0.0f, 0.30f)) : 0.4f;
                            d.rot_y = (opt.randomize_deco_rotation && rng_float(drng, 0.0f, 1.0f) < 0.6f) ? -1.5707963f : 0.0f;
                            // Prefer the smallest rock pod for satellites.
                            d.pod = smallest_rock_pod(rocks, anchor);
                            const std::string b = build_deco_object(d, "deco_rock" + std::to_string(++deco_idx));
                            if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
                        }
                    });
                }

                // Pots / breakable props — placed upright on the ground at a
                // steady, guaranteed rate (they are loot props, not scree).
                if (!props.empty()) {
                    const float sp = std::clamp(360.0f / (dens + 1e-3f), 300.0f, 1100.0f);
                    scatter_pass(sp, 0x70712u, [&](float x, float gy) {
                        // Occasional pair of pots.
                        const int cnt = (rng_float(drng, 0.0f, 1.0f) < 0.25f) ? 2 : 1;
                        for (int ci = 0; ci < cnt; ++ci) {
                            Deco d;
                            d.x = x + (ci ? rng_float(drng, 22.0f, 44.0f) : rng_float(drng, -10.0f, 10.0f));
                            d.y = gy - 1.0f;                 // seated on the ground
                            d.z = rng_float(drng, -2.0f, 6.0f);
                            d.scale = opt.randomize_deco_scale ? (0.85f + rng_float(drng, 0.0f, 0.35f)) : 1.0f;
                            d.pod = pick(props, local_hash_uint((uint32_t)(deco_idx * 251 + 19)), "pot");
                            const std::string b = build_deco_object(d, "deco_prop" + std::to_string(++deco_idx));
                            if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
                        }
                    });
                }

                // Foreground decos (Tier 5: +22..+71).
                if (!bio.deco_fg.empty()) {
                    const float sp = std::clamp(200.0f / (dens + 1e-3f), 100.0f, 500.0f);
                    scatter_pass(sp, 0xF0E1u, [&](float x, float gy) {
                        Deco d;
                        d.x = x + rng_float(drng, -40.0f, 40.0f);
                        d.y = gy + rng_float(drng, -5.0f, 5.0f);
                        d.z = rng_float(drng, 22.0f, 71.0f);
                        d.scale = opt.randomize_deco_scale ? (0.4f + rng_float(drng, 0.0f, 0.5f)) : 1.0f;
                        d.pod = pick(bio.deco_fg, local_hash_uint((uint32_t)(deco_idx * 233 + 17)), "bush");
                        const std::string b = build_deco_object(d, "fg_deco" + std::to_string(++deco_idx));
                        if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
                    });
                }

                // Background parallax decos on main layer (Tier 1+2: -160..-49).
                if (!bio.deco_deep_bg.empty()) {
                    const float sp = std::clamp(220.0f / (dens + 1e-3f), 120.0f, 600.0f);
                    scatter_pass(sp, 0xBEEFu, [&](float x, float gy) {
                        const int cnt = 1 + (int)(rng_float(drng, 0.0f, 1.0f) < 0.35f ? 1 : 0)
                                          + (int)(rng_float(drng, 0.0f, 1.0f) < 0.15f ? 1 : 0);
                        for (int ci = 0; ci < cnt; ++ci) {
                            Deco d;
                            d.x = x + rng_float(drng, -80.0f, 80.0f);
                            d.y = gy + rng_float(drng, -10.0f, 20.0f);
                            d.z = (rng_float(drng, 0.0f, 1.0f) < 0.40f)
                                ? rng_float(drng, -160.0f, -113.0f)
                                : rng_float(drng, -112.0f, -49.0f);
                            d.scale = opt.randomize_deco_scale
                                ? ((d.z < -112.0f) ? (1.6f + rng_float(drng, 0.0f, 1.2f))
                                                   : (1.0f + rng_float(drng, 0.0f, 0.8f)))
                                : 1.0f;
                            d.rot_y = (opt.randomize_deco_rotation && rng_float(drng, 0.0f, 1.0f) < 0.5f) ? -1.5707963f : 0.0f;
                            d.pod = pick(bio.deco_deep_bg, local_hash_uint((uint32_t)(deco_idx * 479 + 31)), "bush");
                            const std::string b = build_deco_object(d, "bg_deco" + std::to_string(++deco_idx));
                            if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
                        }
                    });
                }

                // Torches / glow — vanilla depth +48..+57; honor DB torch pod.
                if (opt.spill_torches && !bio.torch_pod.empty()) {
                    const float base_sp = (opt.torch_spacing > 0.0f) ? opt.torch_spacing : 240.0f;
                    const float sp = base_sp * 1.6f;
                    const bool grove_torch = (bio.torch_pod == "grove_torch");
                    int slot = 0;
                    scatter_pass(sp, 0x70C4u, [&](float x, float gy) {
                        const bool real_torch = (slot++ % 3) == 0;
                        TorchLight tl;
                        tl.x = x + rng_float(drng, -sp * 0.15f, sp * 0.15f);
                        tl.y = gy + 6.0f;
                        tl.z = rng_float(drng, 48.0f, 57.0f);
                        tl.glow_r = bio.torch_glow[0]; tl.glow_g = bio.torch_glow[1]; tl.glow_b = bio.torch_glow[2];
                        std::string b;
                        if (real_torch) {
                            tl.radius    = bio.torch_radius > 0.0f ? bio.torch_radius : 350.0f;
                            tl.intensity = bio.torch_intensity > 0.0f ? bio.torch_intensity : 2.0f;
                            if (grove_torch) {
                                tl.glow_r = std::max(0.51f, tl.glow_r); // selects grove_torch pod
                                b = build_torch_object(tl, "torch" + std::to_string(++torch_idx));
                            } else {
                                tl.glow_r = std::min(0.49f, tl.glow_r); // selects plain torch pod
                                b = build_torch_object(tl, "torch" + std::to_string(++torch_idx));
                            }
                        } else {
                            tl.radius    = 180.0f + rng_float(drng, 0.0f, 80.0f);
                            tl.intensity = 1.1f;
                            tl.glow_a    = 0.35f;
                            b = build_glow_light(tl, "glow" + std::to_string(++glow_idx));
                        }
                        if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
                    });
                }
            } else {
                // Non-walkable bg layer: sparse deep-bg deco scatter only.
                if (!bio.deco_deep_bg.empty()) {
                    const float sp = std::clamp(240.0f / (dens + 1e-3f), 200.0f, 1200.0f);
                    scatter_pass(sp, 0xA99Bu, [&](float x, float gy) {
                        Deco d;
                        d.x = x + rng_float(drng, -60.0f, 60.0f);
                        d.y = gy + rng_float(drng, -5.0f, 10.0f);
                        d.z = layer.z + rng_float(drng, -15.0f, 15.0f);
                        d.scale = opt.randomize_deco_scale
                            ? ((layer.z < -50.0f) ? (1.2f + rng_float(drng, 0.0f, 0.8f))
                                                  : (0.4f + rng_float(drng, 0.0f, 0.4f)))
                            : 1.0f;
                        d.rot_y = (opt.randomize_deco_rotation && rng_float(drng, 0.0f, 1.0f) < 0.5f) ? -1.5707963f : 0.0f;
                        d.pod = pick(bio.deco_deep_bg, local_hash_uint((uint32_t)(deco_idx * 131 + 7)), "bush");
                        const std::string b = build_deco_object(d, "deco_bg" + std::to_string(++deco_idx));
                        if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
                    });
                }
            }
        } // strips
    } // layers

    // ── Water (from DB) ───────────────────────────────────────────────────
    if (opt.add_water && bio.water_enabled && bio.water_front[3] > 0.01f) {
        const float water_top = floor_y - 80.0f;
        Water w;
        w.rect[0] = -half_w - 120.0f;
        w.rect[1] = water_top;
        w.rect[2] = opt.width + 240.0f;
        w.rect[3] = 50.0f;
        memcpy(w.front_rgba,   bio.water_front,   sizeof(float) * 4);
        memcpy(w.surface_rgba, bio.water_surface, sizeof(float) * 4);
        const std::string b = build_water_object(w, "water");
        if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // v3.1 ADVANCED SYSTEMS — portals, breakables, collectibles, particles,
    // fire, moving platforms, ambient lights, sounds.
    // All derived from 167 decoded vanilla scenes census data.
    // ═══════════════════════════════════════════════════════════════════════

    // Helper: find the ground Y at a given X across all walkable platforms.
    auto find_ground_y_at_x = [&](float tx) -> float {
        float best_top = -1e9f;
        for (const auto& pp : walkable_plats) {
            std::vector<Vec2> poly = pp.polygon;
            if (poly.size() < 3)
                poly = make_rect_polygon(pp.rect[0], pp.rect[1], pp.rect[2], pp.rect[3]);
            float pl_minx = 1e9f, pl_maxx = -1e9f, top = -1e9f;
            for (const auto& v : poly) {
                pl_minx = std::min(pl_minx, v.x);
                pl_maxx = std::max(pl_maxx, v.x);
                top = std::max(top, v.y);
            }
            if (tx >= pl_minx - 12.0f && tx <= pl_maxx + 12.0f)
                best_top = std::max(best_top, top);
        }
        return best_top;
    };
    // Helper: find the leftmost and rightmost walkable platform edges.
    float walk_minx = 1e9f, walk_maxx = -1e9f;
    for (const auto& pp : walkable_plats) {
        std::vector<Vec2> poly = pp.polygon;
        if (poly.size() < 3)
            poly = make_rect_polygon(pp.rect[0], pp.rect[1], pp.rect[2], pp.rect[3]);
        for (const auto& v : poly) {
            walk_minx = std::min(walk_minx, v.x);
            walk_maxx = std::max(walk_maxx, v.x);
        }
    }

    // ── PORTALS at scene extremes (left + right) ─────────────────────────
    // Vanilla scenes have 3-8 portals connecting to adjacent scenes.
    // We place one at each walkable extreme with full decoration cluster.
    int portal_count = 0;
    {
        const auto& dests = bio.portal_dests;
        // Left portal (exits to left-connected scene)
        if (!dests.empty()) {
            float lx = walk_minx + 200.0f;
            float ly = find_ground_y_at_x(lx);
            if (ly > -1e8f) {
                PortalHubOptions ph;
                ph.x = lx; ph.y = ly + 56.0f;
                ph.facing = -1;
                ph.destination = dests[0];
                ph.portal_name = "portal_left";
                auto hub = build_portal_hub(ph, (Biome)(int)opt.biome, drng);
                for (const auto& obj : hub) {
                    std::string bare = local_extract_object_bytes(obj);
                    if (!bare.empty()) { scene.write_bytes_field(1, bare); ++r.objects; }
                }
                ++portal_count;
            }
        }
        // Right portal (exits to right-connected scene)
        if (dests.size() > 1) {
            float rx = walk_maxx - 200.0f;
            float ry = find_ground_y_at_x(rx);
            if (ry > -1e8f) {
                PortalHubOptions ph;
                ph.x = rx; ph.y = ry + 56.0f;
                ph.facing = 1;
                ph.destination = dests[1];
                ph.portal_name = "portal_right";
                auto hub = build_portal_hub(ph, (Biome)(int)opt.biome, drng);
                for (const auto& obj : hub) {
                    std::string bare = local_extract_object_bytes(obj);
                    if (!bare.empty()) { scene.write_bytes_field(1, bare); ++r.objects; }
                }
                ++portal_count;
            }
        }
        // Optional third portal (cave/interior exit) at a mid-walkable spot
        if (dests.size() > 2 && walkable_plats.size() > 2) {
            float mx = (walk_minx + walk_maxx) * 0.5f;
            float my = find_ground_y_at_x(mx);
            if (my > -1e8f) {
                PortalHubOptions ph;
                ph.x = mx; ph.y = my + 56.0f;
                ph.facing = (rng_float(drng, 0.0f, 1.0f) < 0.5f) ? -1 : 1;
                ph.destination = dests[2];
                ph.portal_name = "portal_cave";
                auto hub = build_portal_hub(ph, (Biome)(int)opt.biome, drng);
                for (const auto& obj : hub) {
                    std::string bare = local_extract_object_bytes(obj);
                    if (!bare.empty()) { scene.write_bytes_field(1, bare); ++r.objects; }
                }
                ++portal_count;
            }
        }
    }

    // ── BREAKABLE OBJECTS (pots, boards, stonepiles) ─────────────────────
    // Vanilla: pots are the #1 most common breakable (876 uses across scenes).
    // They appear on walkable platforms at a steady rate with CollisionShape.
    {
        const auto& brk = bio.breakables;
        if (!brk.empty() && !walkable_plats.empty()) {
            const float dens = std::clamp(opt.deco_density, 0.0f, 2.0f);
            const float spacing = std::clamp(380.0f / (dens + 1e-3f), 280.0f, 1200.0f);
            int bi = 0;
            for (const auto& pp : walkable_plats) {
                std::vector<Vec2> poly = pp.polygon;
                if (poly.size() < 3)
                    poly = make_rect_polygon(pp.rect[0], pp.rect[1], pp.rect[2], pp.rect[3]);
                float pl_minx = 1e9f, pl_maxx = -1e9f, top = -1e9f;
                for (const auto& v : poly) {
                    pl_minx = std::min(pl_minx, v.x);
                    pl_maxx = std::max(pl_maxx, v.x);
                    top = std::max(top, v.y);
                }
                uint64_t s = ((uint64_t)seed << 2) ^ (uint64_t)(bi++ * 0x4321u);
                float x = pl_minx + rng_float(s, 80.0f, spacing);
                while (x < pl_maxx - 80.0f) {
                    const std::string tmpl = pick(brk, local_hash_uint((uint32_t)(bi * 199 + x)), brk[0].c_str());
                    const std::string b = build_breakable_object(
                        tmpl, "breakable" + std::to_string(bi),
                        x, top - 2.0f, rng_float(drng, -4.0f, 6.0f));
                    if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
                    x += spacing * rng_float(s, 0.7f, 1.3f);
                    ++bi;
                }
            }
        }
    }

    // ── COLLECTIBLE ITEMS (nugget_health, nugget_mana, shards) ───────────
    // Vanilla: nugget_mana appears 20× in florennum alone; health/mana nuggets
    // are scattered on walkable platforms and near platforms edges.
    {
        const auto& cols = bio.collectibles;
        if (!cols.empty() && !walkable_plats.empty()) {
            int ci = 0;
            for (const auto& pp : walkable_plats) {
                std::vector<Vec2> poly = pp.polygon;
                if (poly.size() < 3)
                    poly = make_rect_polygon(pp.rect[0], pp.rect[1], pp.rect[2], pp.rect[3]);
                float pl_minx = 1e9f, pl_maxx = -1e9f, top = -1e9f;
                for (const auto& v : poly) {
                    pl_minx = std::min(pl_minx, v.x);
                    pl_maxx = std::max(pl_maxx, v.x);
                    top = std::max(top, v.y);
                }
                // 2-3 collectibles per platform (vanilla density)
                const int n_col = 2 + (int)(rng_float(drng, 0.0f, 1.0f) < 0.4f ? 1 : 0);
                for (int k = 0; k < n_col; ++k) {
                    const float cx = pl_minx + rng_float(drng, 0.15f, 0.85f) * (pl_maxx - pl_minx);
                    const std::string tmpl = pick(cols, local_hash_uint((uint32_t)(ci * 277 + k)), cols[0].c_str());
                    const std::string b = build_collectable_object(
                        tmpl, "collectable" + std::to_string(ci),
                        cx, top + rng_float(drng, 8.0f, 28.0f), // floating above ground
                        rng_float(drng, -3.0f, 5.0f));
                    if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
                    ++ci;
                }
            }
        }
    }

    // ── AMBIENT PARTICLE EMITTERS ────────────────────────────────────────
    // Vanilla: worldsend scenes have ParticleEmitter for atmospheric dust/snow.
    // Placed at the biome's center X, spread across the walkable area.
    {
        const auto& parts = bio.particles;
        if (!parts.empty()) {
            const int n_particles = std::clamp((int)(opt.width / 800.0f), 2, 8);
            for (int i = 0; i < n_particles; ++i) {
                const float px = -half_w + (float)(i + 1) * opt.width / (float)(n_particles + 1);
                const float py = find_ground_y_at_x(px);
                if (py < -1e8f) continue;
                const std::string tmpl = pick(parts, local_hash_uint((uint32_t)(i * 431)), parts[0].c_str());
                const std::string b = build_particle_object(
                    tmpl, "particle" + std::to_string(i),
                    px, py + rng_float(drng, 50.0f, 150.0f), rng_float(drng, -20.0f, 30.0f));
                if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
            }
        }
    }

    // ── FIRE EMITTERS (fire biome ambient) ───────────────────────────────
    // Vanilla fire_part scenes use shadowblob_fire_little / firesprayer templates.
    {
        const auto& fires = bio.fire_fx;
        if (!fires.empty()) {
            const int n_fire = std::clamp((int)(opt.width / 600.0f), 3, 12);
            for (int i = 0; i < n_fire; ++i) {
                const float fx = -half_w + (float)(i + 1) * opt.width / (float)(n_fire + 1);
                const float fy = find_ground_y_at_x(fx);
                if (fy < -1e8f) continue;
                const std::string tmpl = pick(fires, local_hash_uint((uint32_t)(i * 571)), fires[0].c_str());
                const std::string b = build_fire_emitter_object(
                    tmpl, "fire_emitter" + std::to_string(i),
                    fx, fy + rng_float(drng, 5.0f, 30.0f), rng_float(drng, -5.0f, 15.0f));
                if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
            }
        }
    }

    // ── MOVING PLATFORMS ──────────────────────────────────────────────────
    // Vanilla: platformwood0/1 (WoodKeep), elevator1 (florennum_jail),
    // flying_platform (icecastle), dropping_groundpiece (snowy).
    {
        const auto& mps = bio.moving_platforms;
        if (!mps.empty()) {
            const int n_mp = std::clamp((int)(opt.width / 1200.0f), 1, 4);
            for (int i = 0; i < n_mp; ++i) {
                const float mx = -half_w * 0.3f + (float)(i + 1) * opt.width * 0.6f / (float)(n_mp + 1);
                const float my = find_ground_y_at_x(mx);
                if (my < -1e8f) continue;
                const std::string tmpl = pick(mps, local_hash_uint((uint32_t)(i * 613)), mps[0].c_str());
                const std::string b = build_moving_platform_object(
                    tmpl, "moving_platform" + std::to_string(i),
                    mx, my + rng_float(drng, 80.0f, 200.0f), // floating above ground
                    rng_float(drng, -5.0f, 10.0f));
                if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
            }
        }
    }

    // ── AMBIENT POINT LIGHTS ─────────────────────────────────────────────
    // Vanilla scenes are LOADED with point lights (1050 Light components total
    // across 167 scenes, ~6 per scene average). We emit biome-specific lights
    // along the walkable area for atmosphere.
    {
        const auto& alights = bio.ambient_lights;
        if (!alights.empty() && !is_cave) {
            const float spacing = std::max(400.0f, opt.width / (float)(alights.size() + 1));
            int li = 0;
            for (float x = -half_w + spacing * 0.5f; x < half_w; x += spacing) {
                const auto& al = alights[li % alights.size()];
                const float gy = find_ground_y_at_x(x);
                if (gy < -1e8f) continue;
                TorchLight tl;
                tl.x = x + al.x_offset;
                tl.y = gy + rng_float(drng, 30.0f, 80.0f);
                tl.z = rng_float(drng, -10.0f, 20.0f);
                tl.radius = al.radius;
                tl.intensity = al.intensity;
                tl.glow_r = al.color[0]; tl.glow_g = al.color[1];
                tl.glow_b = al.color[2]; tl.glow_a = 0.4f;
                const std::string b = build_glow_light(tl, "amb_light" + std::to_string(li));
                if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
                ++li;
            }
        }
    }

    // ── AMBIENT SOUND EFFECTS ────────────────────────────────────────────
    // Vanilla scenes place SoundEffect objects at key positions.
    {
        if (!bio.sound_ambient.empty()) {
            const int n_sounds = std::clamp((int)(opt.width / 1000.0f), 1, 5);
            for (int i = 0; i < n_sounds; ++i) {
                const float sx = -half_w + (float)(i + 1) * opt.width / (float)(n_sounds + 1);
                const float sy = find_ground_y_at_x(sx);
                if (sy < -1e8f) continue;
                const std::string b = build_sound_object(
                    bio.sound_ambient, "sound" + std::to_string(i),
                    sx, sy, 500.0f);
                if (!b.empty()) { scene.write_bytes_field(1, local_extract_object_bytes(b)); ++r.objects; }
            }
        }
    }

    // ── Spawn (v2 parity: platform under x=0) ─────────────────────────────
    float sx = 0.0f, sy = (maxy > -1e8f ? maxy : 0.0f) + 56.0f;
    {
        float best_top = -1e9f;
        for (const auto& pp : walkable_plats) {
            std::vector<Vec2> poly = pp.polygon;
            if (poly.size() < 3)
                poly = make_rect_polygon(pp.rect[0], pp.rect[1], pp.rect[2], pp.rect[3]);
            float min_x = 1e9f, max_x = -1e9f, top = -1e9f;
            for (const auto& v : poly) {
                min_x = std::min(min_x, v.x);
                max_x = std::max(max_x, v.x);
                top   = std::max(top, v.y);
            }
            if (0.0f >= min_x - 12.0f && 0.0f <= max_x + 12.0f)
                best_top = std::max(best_top, top);
        }
        if (best_top > -1e8f) sy = best_top + 56.0f;
    }
    scene.write_bytes_field(1, local_extract_object_bytes(
        build_spawn_object("spawn_default", sx, sy, 1)));
    ++r.objects;

    // ── ObjectLibrary ─────────────────────────────────────────────────────
    static const std::vector<std::string> kImports = {
        "caves_stuff", "collectibles", "florennum_stuff", "forest",
        "game_common", "grovestuff", "lights", "monsters", "npc",
        "plains_stuff", "platforms", "playground", "programs", "rocks",
        "scriptarea", "traps_stuffs", "trash", "woodkeep_stuff", "woods",
    };
    scene.write_bytes_field(2, build_object_library(opt.scene_name, kImports));

    // ── Root Bounds (walkable band — v2 parity) ───────────────────────────
    if (maxx <= minx || maxy <= miny) { r.error = "v3: empty scene AABB"; return r; }
    if (top_min >= 1e30f) { top_min = miny; top_max = maxy; }
    const float pad  = 220.0f;
    const float head_up = is_cave ? (kCaveGap + 400.0f) : 350.0f;
    const float head_dn = 650.0f;
    const float bx = minx - pad;
    const float by = top_min - head_dn;
    const float bw = (maxx - minx) + 2.0f * pad;
    const float bh = (top_max + head_up) - by;
    scene.write_bytes_field(3, build_bounds_payload(bx, by, bw, bh));

    r.bounds[0] = bx; r.bounds[1] = by; r.bounds[2] = bw; r.bounds[3] = bh;
    r.scene_bytes = scene.to_string();
    return r;
}

} // namespace v3
} // namespace sgen
