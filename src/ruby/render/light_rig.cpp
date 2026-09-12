// ============================================================================
// light_rig.cpp — LightRig implementation, tuned for Swordigo worlds.
//
//   Swordigo visual style (from asset research):
//     - Warm, high-contrast, saturated fantasy palette (forest, dungeon, village)
//     - Original GLES1.1: simple fixed-function warm-key + cool-fill + ambient
//     - V7.1 desktop port: Reinhard + sat boost, "warm sun / cool sky" three-point
//     - Terrain: diffuse-only (no specular maps); models: diffuse + material tint
//
//   Three-point rig:
//     [0] Key   — warm amber-gold sun from yaw/pitch (hero light of the scene)
//     [1] Fill  — cool blue-violet from opposite direction (sky bounce)
//     [2] Bounce— soft warm earth bounce from below the horizon (rim from ground)
//
//   Hemisphere ambient:
//     sky    — desaturated sky blue (what Godot calls "sky ambient")
//     ground — warm ochre/earth (what bounces from grass / soil / stone)
// ============================================================================

#include "light_rig.h"
#include "viewport_shader.h"
#include "../viewport/viewport_lighting.h"

#include <cmath>
#include <cstring>
#include <algorithm>

namespace ruby::render {

// ── LightRig::build_from_viewport_lighting ────────────────────────────────────
void LightRig::build_from_viewport_lighting(const ruby::viewport::ViewportLighting& vl) {
    const float pi = 3.14159265358979323846f;
    const float ky = vl.key_yaw   * pi / 180.0f;
    const float kp = vl.key_pitch * pi / 180.0f;

    const float ki  = std::clamp(vl.key_intensity,    0.0f, 4.0f);
    const float fi  = std::clamp(vl.fill_intensity,   0.0f, 2.0f);
    const float bi  = std::clamp(vl.bounce_intensity, 0.0f, 1.0f);
    const float amb = std::clamp(vl.ambient,          0.0f, 2.0f);

    // ── [0] Key light — warm amber-gold sun ───────────────────────────────────
    // Swordigo has a warm, "adventurous daylight" look. The key light leans
    // amber-gold rather than neutral white to give terrain and characters a
    // sun-lit, warm cast. Slightly oversaturated vs real daylight — intentional
    // for the game's vibrant palette.
    const float kp_cos = std::cos(kp);
    dir_lights[0].dir[0]    = kp_cos * std::cos(ky);
    dir_lights[0].dir[1]    = std::sin(kp);
    dir_lights[0].dir[2]    = kp_cos * std::sin(ky);
    // Warm amber-gold: red >> green >> blue
    dir_lights[0].color[0]  = ki * 1.05f;   // red:   full + small boost
    dir_lights[0].color[1]  = ki * 0.88f;   // green: moderate warm
    dir_lights[0].color[2]  = ki * 0.65f;   // blue:  pulled down for warmth
    dir_lights[0].intensity = 1.0f;
    dir_lights[0].enabled   = true;

    // ── [1] Fill light — cool sky blue from opposite direction ────────────────
    // Lifts shadow sides so dark geometry is still readable (important for
    // Swordigo's cave/dungeon environments). Cool blue balances the warm key.
    // Placed at low elevation — it's the sky fill, not a secondary sun.
    const float fy     = ky + pi;
    const float fp     = -kp * 0.30f;    // slightly below horizon
    const float fp_cos = std::cos(fp);
    dir_lights[1].dir[0]    = fp_cos * std::cos(fy);
    dir_lights[1].dir[1]    = std::sin(fp);
    dir_lights[1].dir[2]    = fp_cos * std::sin(fy);
    // Cool blue-violet: low red, moderate green, high blue
    dir_lights[1].color[0]  = fi * 0.40f;  // red:   pulled down
    dir_lights[1].color[1]  = fi * 0.52f;  // green: moderate
    dir_lights[1].color[2]  = fi * 0.80f;  // blue:  dominant — cool sky
    dir_lights[1].intensity = 1.0f;
    dir_lights[1].enabled   = true;

    // ── [2] Bounce light — warm earth/grass bounce from below ─────────────────
    // Simulates light bouncing off the terrain (green grass, brown soil, stone).
    // Comes from slightly above to be visible. Low intensity — it's a fill, not
    // a primary source. Warm tint matches Swordigo's earthy terrain palette.
    dir_lights[2].dir[0]    =  0.15f;   // slight offset for more interesting shading
    dir_lights[2].dir[1]    =  0.95f;   // mostly upward: overhead sky bounce
    dir_lights[2].dir[2]    = -0.28f;
    // Normalize
    {
        float len = std::sqrt(dir_lights[2].dir[0]*dir_lights[2].dir[0]
                            + dir_lights[2].dir[1]*dir_lights[2].dir[1]
                            + dir_lights[2].dir[2]*dir_lights[2].dir[2]);
        dir_lights[2].dir[0] /= len;
        dir_lights[2].dir[1] /= len;
        dir_lights[2].dir[2] /= len;
    }
    // Warm ochre: reddish-green, no blue — earth bounce
    dir_lights[2].color[0]  = bi * 0.55f;  // red:   moderate warm
    dir_lights[2].color[1]  = bi * 0.60f;  // green: matches earthy terrain
    dir_lights[2].color[2]  = bi * 0.28f;  // blue:  very low — warm earth tone
    dir_lights[2].intensity = 1.0f;
    dir_lights[2].enabled   = true;

    dir_light_count = 3;

    // ── Hemisphere ambient ────────────────────────────────────────────────────
    // Sky: soft blue-grey (overcast sky visible through forest canopy in Swordigo)
    // Ground: warm ochre-brown (grass, soil, stone ground plane)
    // These don't light the scene strongly — they're the constant "ambient bath"
    // that prevents any face from going completely black.
    sky.sky_color[0]    = amb * 0.32f;   // muted blue-grey sky
    sky.sky_color[1]    = amb * 0.36f;
    sky.sky_color[2]    = amb * 0.48f;
    sky.ground_color[0] = amb * 0.38f;   // warm ochre earth
    sky.ground_color[1] = amb * 0.30f;
    sky.ground_color[2] = amb * 0.20f;
    sky.intensity = 1.0f;

    // ── Fog ───────────────────────────────────────────────────────────────────
    fog.enabled  = vl.fog_enabled;
    fog.density  = std::clamp(vl.fog_density, 0.0f, 0.05f);
    // Swordigo fog is a dark warm haze — not pure grey
    fog.color[0] = 0.10f;  // dark warm
    fog.color[1] = 0.08f;
    fog.color[2] = 0.07f;

    // Exposure: 1.15 base (tuned in shader init), LightRig just passes it through
    exposure = 1.0f;
}

// ── LightRig::upload_to_shader ────────────────────────────────────────────────
void LightRig::upload_to_shader(ViewportShader& shader, const float view[16]) const {
    // Transform world-space light dirs to view space.
    // view is column-major: view[col*4 + row]
    auto to_view = [&](const float d[3], float out[3]) {
        out[0] = view[0]*d[0] + view[4]*d[1] + view[8]*d[2];
        out[1] = view[1]*d[0] + view[5]*d[1] + view[9]*d[2];
        out[2] = view[2]*d[0] + view[6]*d[1] + view[10]*d[2];
    };

    ViewportShader::DirLightData view_lights[k_max_dir_lights];
    int active = 0;
    for (int i = 0; i < dir_light_count && i < k_max_dir_lights; ++i) {
        if (!dir_lights[i].enabled) continue;
        to_view(dir_lights[i].dir, view_lights[active].dir);
        view_lights[active].color[0] = dir_lights[i].color[0] * dir_lights[i].intensity;
        view_lights[active].color[1] = dir_lights[i].color[1] * dir_lights[i].intensity;
        view_lights[active].color[2] = dir_lights[i].color[2] * dir_lights[i].intensity;
        ++active;
    }
    shader.setDirLights(view_lights, active);

    // Hemisphere ambient
    float s_sky[3]    = { sky.sky_color[0]    * sky.intensity,
                          sky.sky_color[1]    * sky.intensity,
                          sky.sky_color[2]    * sky.intensity };
    float s_ground[3] = { sky.ground_color[0] * sky.intensity,
                          sky.ground_color[1] * sky.intensity,
                          sky.ground_color[2] * sky.intensity };
    shader.setAmbient(s_sky, s_ground);

    // World-up (0,1,0) in view space: column 1 of view matrix (col-major indices [4],[5],[6])
    shader.setWorldUpView(view[4], view[5], view[6]);

    // Fog
    shader.setFog(fog.enabled, fog.color, fog.density);

    // Exposure (base 1.0 from rig — shader init defaults to 1.15)
    shader.setExposure(exposure);
}

} // namespace ruby::render
