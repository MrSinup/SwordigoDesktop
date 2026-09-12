#pragma once
// ============================================================================
// viewport_lighting.h — shared lighting rig for the 3D viewport.
//   Mirrors the lighting dock panel controls: adjustable sun (key) light,
//   opposite fill, overhead bounce, scene ambient, and depth fog.
//   Pure data — read by Viewport3DWidget each frame via LightRig, edited
//   from the Lighting dock panel.
//
//   Default values tuned for Swordigo worlds:
//     - Warm amber-gold key at classic 3/4 angle
//     - Strong enough fill/bounce to see cave/dungeon geometry
//     - Generous ambient so shadow areas have color, not black
// ============================================================================

namespace ruby::viewport {

struct ViewportLighting {
    // ── Sun / key directional light ──────────────────────────────────────────
    float key_intensity   = 1.10f;  // 0..4  — warm amber sun
    int   key_yaw         = -55;    // azimuth, degrees (3/4 left of camera)
    int   key_pitch       = 42;     // elevation, degrees (mid-high sun angle)

    // ── Fill + bounce ────────────────────────────────────────────────────────
    float fill_intensity   = 0.42f; // 0..2  — cool sky fill (reads into shadows)
    float bounce_intensity = 0.22f; // 0..1  — warm earth/grass bounce

    // ── Global ambient (minimum scene light level) ────────────────────────────
    // Higher than before: Swordigo has colorful shadows, not black ones.
    float ambient = 0.52f;          // 0..2

    // ── Depth fog ────────────────────────────────────────────────────────────
    bool  fog_enabled = false;
    float fog_density = 0.00035f;   // EXP2 density
};

} // namespace ruby::viewport
