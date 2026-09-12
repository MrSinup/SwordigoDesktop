#pragma once
// ============================================================================
// shader_library.h — GLSL 330 snippet registry for Ruby GG render shaders.
//   Header-only. Returns compile-time string constants that are concatenated
//   into full shader source strings in viewport_shader.cpp.
//
//   Snippets are tuned specifically for Swordigo worlds (low-poly mobile assets,
//   PVRTC textures decoded to RGBA8 without sRGB tagging, no specular maps,
//   no normal maps, POD material has diffuse color + opacity only).
//
//   Snippet index:
//     "math"           — common helpers (saturate, sq, M_PI)
//     "halflambert"    — Half-Lambert² directional diffuse (Valve / stylized games)
//     "hemisphere"     — hemisphere_ambient()
//     "tonemap"        — Reinhard (white=2.5, L_white from V7.1 desktop port)
//                        + swordigo_grade() (+15% saturation, warm push)
//     "fog_exp2"       — apply_fog_exp2()
// ============================================================================

namespace ruby::render {

class ShaderLibrary {
public:
    static const char* get(const char* name) {
        if (__builtin_strcmp(name, "math")        == 0) return k_math;
        if (__builtin_strcmp(name, "halflambert") == 0) return k_halflambert;
        if (__builtin_strcmp(name, "hemisphere")  == 0) return k_hemisphere;
        if (__builtin_strcmp(name, "tonemap")     == 0) return k_tonemap;
        if (__builtin_strcmp(name, "fog_exp2")    == 0) return k_fog_exp2;
        return "";
    }

private:
    // ── math ─────────────────────────────────────────────────────────────────
    static constexpr const char* k_math = R"GLSL(
#define M_PI 3.14159265358979323846
float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3  saturate(vec3 v)  { return clamp(v, vec3(0.0), vec3(1.0)); }
float sq(float x)       { return x * x; }
)GLSL";

    // ── halflambert ──────────────────────────────────────────────────────────
    // Half-Lambert² directional diffuse — Valve Research, 2004.
    // Designed specifically for stylized games: wraps the diffuse terminator
    // so dark sides are always readable, eliminates harsh shadow clipping.
    //
    // Formula: wrap = saturate(NdotL * 0.5 + 0.5)²
    //   NdotL = 0.0 (perpendicular) → wrap = 0.25  (never fully dark)
    //   NdotL = 1.0 (facing)        → wrap = 1.0   (full brightness)
    //   NdotL = -1.0 (backface)     → wrap = 0.0   (dark, not negative)
    //
    // Returns unscaled contribution — caller multiplies by light_color * albedo.
    static constexpr const char* k_halflambert = R"GLSL(
// ── Half-Lambert² Directional Diffuse ────────────────────────────────────────
// Source: Valve "Illustrative Rendering in Team Fortress 2" (2007),
//         adapted from Half-Life 2 (2004) shader pipeline.
// Ideal for low-poly stylized meshes (Swordigo: mobile GLES1.1 assets).
// No specular term — Swordigo POD materials have no gloss or specular maps.
float halflambert(float NdotL) {
    float wrap = NdotL * 0.5 + 0.5;
    return wrap * wrap;
}
)GLSL";

    // ── hemisphere ───────────────────────────────────────────────────────────
    static constexpr const char* k_hemisphere = R"GLSL(
// ── Hemisphere Ambient ───────────────────────────────────────────────────────
// Standard sky/ground gradient driven by surface normal vs world up.
// world_up: world-space (0,1,0) transformed into current shading space (view).
// Returns ambient color clamped above a minimum of 0.04 to avoid pure black.
vec3 hemisphere_ambient(vec3 N, vec3 sky_color, vec3 ground_color, vec3 world_up) {
    float weight = dot(N, normalize(world_up)) * 0.5 + 0.5;
    return max(mix(ground_color, sky_color, clamp(weight, 0.0, 1.0)), vec3(0.04));
}
)GLSL";

    // ── tonemap ──────────────────────────────────────────────────────────────
    // Reinhard Extended (white-point 2.5) — matches the V7.1 Swordigo Desktop
    // PostFX pipeline used in the reference build.
    //
    // The V7.1 pipeline additionally applied:
    //   - +15% saturation boost (swordigo_grade)
    //   - Warm luminosity push (slight orange-yellow tint in highlights)
    //   - Soft-knee bloom around torches/portals (not in shader level)
    //
    // No ACES: ACES mutes Swordigo's saturated palette and shifts the warm
    // fantasy colors toward a cool, desaturated film look. Reinhard preserves
    // the original vibrant green/amber tones.
    static constexpr const char* k_tonemap = R"GLSL(
// ── Extended Reinhard Tone Mapping ────────────────────────────────────────────
// L_white = 2.5: matches V7.1 Swordigo Desktop port PostFX calibration.
// Preserves hue and saturation, only compresses HDR luminance.
// Formula: L_out = L * (1 + L/L_white²) / (1 + L)
vec3 tonemap_reinhard(vec3 color) {
    const float white_sq = 2.5 * 2.5;  // L_white² = 6.25
    return (color * (1.0 + color / white_sq)) / (1.0 + color);
}

// ── Swordigo Color Grade ──────────────────────────────────────────────────────
// +15% saturation boost: V7.1 PostFX pipeline characteristic.
// Warm luminosity push: slight orange-yellow cast in highlights matching the
// "fantastical adventure" look of Swordigo's art direction.
// Input: tonemapped linear [0,1] color.
// Output: graded color ready for display output.
vec3 swordigo_grade(vec3 color) {
    // Saturation boost: move toward/away from luminance (+15%)
    // luma: perceptual weighting for saturation math
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    vec3 sat   = mix(vec3(luma), color, 1.15);          // +15% saturation

    // Warm push: subtle orange-yellow tint in lit areas
    // Dark areas stay cool/neutral; bright areas lean warm
    float warmth = luma * luma;                         // square: only in highlights
    sat.r += warmth * 0.028;                            // push red slightly
    sat.g += warmth * 0.012;                            // push green slightly
    sat.b -= warmth * 0.018;                            // pull blue slightly

    return clamp(sat, 0.0, 1.0);
}

// ── Mild Gamma Output ─────────────────────────────────────────────────────────
// Swordigo textures are PVRTC/ETC1 decoded to RGBA8 without GL_SRGB tagging.
// The original GLES1.1 renderer didn't gamma-correct either.
// We apply a mild pow(1/1.6) to compensate for the linear framebuffer on
// sRGB displays — brighter than pow(1/2.2) since we don't decode textures.
vec3 gamma_out(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(1.0 / 1.6));
}
)GLSL";

    // ── fog_exp2 ─────────────────────────────────────────────────────────────
    static constexpr const char* k_fog_exp2 = R"GLSL(
// ── EXP2 Depth Fog ────────────────────────────────────────────────────────────
// dist: fragment distance from camera (length(vViewPos) in view space).
// density: GL_EXP2 fog density — typical Swordigo range 0.0001..0.005.
vec3 apply_fog_exp2(vec3 color, vec3 fog_color, float density, float dist) {
    float f = exp(-(density * density * dist * dist));
    return mix(fog_color, color, clamp(f, 0.0, 1.0));
}
)GLSL";
};

} // namespace ruby::render
