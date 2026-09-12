#pragma once
// visual.h — components → draw items.
//
// Every renderable component in Swordigo contributes a *thing* to the frame and
// nothing else: a Model names a POD, a Sprite names a texture, a Light has an
// intensity/radius/colour, a ParticleEmitter has a particle and a cadence, a
// Shadow has two radii. Assembling those into the draw list is the missing step
// between the recovered runtime (runtime.h) and whatever renders it — the Ruby
// viewport today, a headless capture or a studio diorama tomorrow.
//
// This is DATA ONLY. It emits no GL, owns no Qt, and never mutates the runtime:
// the caller decides whether to feed it to the existing viewport, to an offline
// capture, or to a thumbnail render. That is why the scl studio can show a
// diorama without touching the 3D viewport's internals.

#include <cstdint>
#include <string>
#include <vector>

namespace caver {

class RuntimeScene;
struct RuntimeObject;
struct ObjectRenderState;

// What kind of thing to draw.
enum class VisualKind : uint8_t {
    Model,        // ModelComponent.Name → POD
    Sprite,       // SpriteComponent.TextureName (billboard)
    Particle,     // ParticleComponent.TextureName (billboard/particle)
    Light,        // LightComponent (point light)
    Glow,         // SimpleGlowComponent (additive billboard)
    Shadow,       // ShadowComponent (projected ellipse)
    Ground,       // GroundPolygonComponent / GroundMeshComponent
    Water,        // WaterMeshComponent
    Background,   // BackgroundComponent
    ShadowMap,    // unused placeholder-free enum value kept for stable indices
    Shape,        // ShapeComponent / CollisionShape gizmo
    WeaponTrail,  // WeaponTrailComponent / WeaponGlowComponent
    Overlay,      // OverlayTextComponent
};

// One draw item, in object-local space; the caller applies the object's
// transform (pos/rot/scale) from the matching ObjectRenderState.
struct VisualItem {
    VisualKind  kind = VisualKind::Model;
    std::string owner;            // object identifier
    std::string asset;            // POD / texture / particle name ("" = procedural)
    int32_t     component = 0;    // the Component.Identifier this came from
    float       pos[3]   = {0, 0, 0};
    float       rot[3]   = {0, 0, 0};
    float       scale[3] = {1, 1, 1};
    float       color[4] = {1, 1, 1, 1};
    float       radius      = 0.0f;   // lights / glows / shadows
    float       intensity   = 1.0f;   // lights
    float       depth       = 0.0f;   // 2.5D draw order for sprites/backgrounds
    float       pulse       = 1.0f;   // SimpleGlow / Light current multiplier
    float       anim_time   = 0.0f;   // animation clock (models/particles)
    float       water_time  = 0.0f;   // water surface clock
    bool        transparent = false;
    bool        additive    = false;  // glows, weapon glows, portal effects
    bool        hidden      = false;
    // Shape gizmos (ShapeComponent / UtilityShape / CollisionShape).
    float       shape_rect[4] = {0, 0, 0, 0};   // x, y, w, h
    float       shape_circle[3] = {0, 0, 0};    // x, y, r
};

// Extract every draw item an object contributes. `state` (optional) supplies the
// live clocks (animation, water, light pulse) so animated items carry the current
// frame's values instead of the authored ones.
std::vector<VisualItem> extract_visuals(const RuntimeObject& object,
                                        const ObjectRenderState* state = nullptr);

// Extract the visuals of a whole scene, in object order.
std::vector<VisualItem> extract_scene_visuals(const RuntimeScene& scene);

// True when a component class contributes a draw item (used by the coverage
// report to keep Stage::Rendered honest).
bool visual_has_item(const std::string& class_name);

// One-line summary of what an object renders — used by the studio's template
// list ("Model board · Light #103 · FireEmitter #105").
std::string visual_summary(const RuntimeObject& object);

} // namespace caver
