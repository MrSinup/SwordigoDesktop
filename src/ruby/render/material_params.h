#pragma once
// ============================================================================
// material_params.h — PBR-lite material parameters for the Ruby GG viewport.
//   Uploaded per draw-call to ViewportShader::setMaterial().
//   Modelled after Raylib pbr.fs conventions (albedo, roughness, metalness)
//   with additions for emissive.
//
//   Energy conservation follows Unreal Engine BRDF.ush:
//     kD = (1 - F) * (1 - metalness)   — metals have zero diffuse
//     F0 = mix(vec3(0.04), albedo, metalness)
// ============================================================================
#include <cstring>

namespace ruby::render {

struct MaterialParams {
    // Albedo / base color tint. Alpha used for transparency.
    // In the shader, albedo is multiplied by the texture sample (if any).
    float base_color[4] = {1.f, 1.f, 1.f, 1.f};

    // PBR scalars — clamped [0..1] in shader
    float roughness = 0.75f;  // 0 = mirror-like specular, 1 = fully diffuse
    float metalness = 0.0f;   // 0 = dielectric (plastic), 1 = metallic

    // Emissive additive color — HDR, can exceed 1 for glowing objects
    float emissive[3] = {0.f, 0.f, 0.f};

    // Albedo texture — set has_albedo_tex=1 and bind the GL texture to albedo_unit
    int has_albedo_tex  = 0;
    int albedo_unit     = 0;   // GL texture unit for the albedo/color map

    // Whether the texture is sRGB-encoded (most viewport assets are).
    // When true the shader applies pow(2.2) decode before lighting.
    int texture_is_srgb = 1;

    // ── Convenience constructors ────────────────────────────────────────────

    static MaterialParams from_color(float r, float g, float b, float a = 1.f) {
        MaterialParams m;
        m.base_color[0] = r; m.base_color[1] = g;
        m.base_color[2] = b; m.base_color[3] = a;
        return m;
    }

    static MaterialParams terrain() {
        return from_color(0.30f, 0.63f, 0.45f);
    }

    static MaterialParams model_default() {
        MaterialParams m;
        m.roughness = 0.60f;
        return m;
    }

    static MaterialParams textured(int gl_unit = 0, bool srgb = true) {
        MaterialParams m;
        m.has_albedo_tex  = 1;
        m.albedo_unit     = gl_unit;
        m.texture_is_srgb = srgb ? 1 : 0;
        return m;
    }
};

} // namespace ruby::render
