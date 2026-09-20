// ============================================================================
// gles_shaders.cpp — Implementation of OpenGL ES 3.0 Mobile Shaders
// ============================================================================

#include "gles_shaders.h"

namespace ruby::android {

static const char* GLES_VS = R"GLSL(#version 300 es
precision mediump float;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in vec2 aUV;

uniform mat4 uProj;
uniform mat4 uModelView;

out vec3 vViewPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 vp      = uModelView * vec4(aPos, 1.0);
    vViewPos     = vp.xyz;
    mat3 normMat = mat3(uModelView);
    vec3 n       = normMat * aNorm;
    float nlen   = length(n);
    vNormal      = (nlen > 1e-5) ? (n / nlen) : vec3(0.0, 1.0, 0.0);
    vUV          = aUV;
    gl_Position  = uProj * vp;
}
)GLSL";

static const char* GLES_FS = R"GLSL(#version 300 es
precision mediump float;

in vec3 vViewPos;
in vec3 vNormal;
in vec2 vUV;

uniform sampler2D uTex;
uniform int       uHasTex;
uniform vec4      uDiffuseColor;

// Lighting uniforms
uniform vec3      uSunDir;       // view-space normalized direction towards sun
uniform vec3      uSunColor;
uniform vec3      uSkyColor;
uniform vec3      uGroundColor;

out vec4 fragColor;

void main() {
    vec4 baseCol = (uHasTex != 0) ? texture(uTex, vUV) : uDiffuseColor;
    if (baseCol.a < 0.05) discard;

    vec3 N = normalize(vNormal);
    vec3 L = normalize(uSunDir);

    // Half-Lambert diffuse (Valve / Swordigo style)
    float NdotL = dot(N, L);
    float halfLambert = NdotL * 0.5 + 0.5;
    float wrapLight = halfLambert * halfLambert;

    // Hemisphere ambient
    float hemi = N.y * 0.5 + 0.5;
    vec3 ambient = mix(uGroundColor, uSkyColor, hemi);

    // Final color accumulation
    vec3 lit = baseCol.rgb * (ambient + uSunColor * wrapLight);

    // Extended Reinhard tone-mapping (L_white = 2.5) matching desktop Ruby GG PostFX
    // Preserves Swordigo's warm saturated palette without crushing brightness
    const float L_white = 2.5;
    lit = lit * (vec3(1.0) + lit / (L_white * L_white)) / (vec3(1.0) + lit);

    // Swordigo color grade: +15% saturation + warm highlight push
    float lum = dot(lit, vec3(0.299, 0.587, 0.114));
    lit = mix(vec3(lum), lit, 1.15);
    lit += vec3(0.04, 0.02, 0.0) * clamp(lum - 0.45, 0.0, 0.55);

    // Linear-to-sRGB gamma output (1/1.6 curve matching desktop engine)
    lit = pow(max(lit, vec3(0.0)), vec3(1.0 / 1.6));

    fragColor = vec4(lit, baseCol.a * uDiffuseColor.a);
}
)GLSL";

static const char* UNLIT_VS = R"GLSL(#version 300 es
precision mediump float;

layout(location = 0) in vec3 aPos;
uniform mat4 uProj;
uniform mat4 uModelView;

void main() {
    gl_Position = uProj * (uModelView * vec4(aPos, 1.0));
}
)GLSL";

static const char* UNLIT_FS = R"GLSL(#version 300 es
precision mediump float;

uniform vec4 uColor;
out vec4 fragColor;

void main() {
    fragColor = uColor;
}
)GLSL";

static const char* OUTLINE_VS = R"GLSL(#version 300 es
precision mediump float;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;

uniform mat4 uProj;
uniform mat4 uModelView;
uniform float uOutlineOffset;

void main() {
    float nlen = length(aNorm);
    vec3 n = (nlen > 1e-5) ? (aNorm / nlen) : vec3(0.0, 1.0, 0.0);
    vec3 pos = aPos + n * uOutlineOffset;
    gl_Position = uProj * (uModelView * vec4(pos, 1.0));
}
)GLSL";

static const char* OUTLINE_FS = R"GLSL(#version 300 es
precision mediump float;

uniform vec4 uColor;
out vec4 fragColor;

void main() {
    fragColor = uColor;
}
)GLSL";

const char* GLESShaders::vertex_shader_source() {
    return GLES_VS;
}

const char* GLESShaders::fragment_shader_source() {
    return GLES_FS;
}

const char* GLESShaders::unlit_vertex_shader_source() {
    return UNLIT_VS;
}

const char* GLESShaders::unlit_fragment_shader_source() {
    return UNLIT_FS;
}

const char* GLESShaders::outline_vertex_shader_source() {
    return OUTLINE_VS;
}

const char* GLESShaders::outline_fragment_shader_source() {
    return OUTLINE_FS;
}

} // namespace ruby::android
