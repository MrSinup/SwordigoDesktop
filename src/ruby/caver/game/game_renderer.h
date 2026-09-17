#pragma once
// game_renderer.h — draws what `caver::` simulates.
//
// WHY THIS EXISTS
// ---------------
// `game_scene_controller.cpp` recovers the game's *simulation* (scene load,
// spawn resolution, hero creation, per-component ticks, the real Lua VM) but it
// deliberately owns no GL: it publishes positions. This is the other half — the
// draw path — and it is a *mapping*, not a second renderer:
//
//   scene data          ->  av::  (scene_loader + av_renderer + pod_loader)
//   runtime transforms  ->  swk:: (object_world_matrix / object_render_matrix)
//
// Everything it draws comes out of the shipped data:
//   * a level object's model is `obj.mesh_name` from its resolved ModelComponent
//     (never a name written down here),
//   * the camera is the engine's own (fov 0.34907 rad, near 50, far 20000, and
//     the (0, 187, 1190) focus offset GameSceneController::InitWithScene writes
//     for the phone device profile — read straight off the binary),
//   * lights, fires, shadows and fluid sheets come from the parsed
//     Light/FireEmitter/Shadow/WaterMesh components,
//   * actors (the hero, and anything Lua spawns) are drawn from the archetype
//     the ObjectLibrary handed the runtime.
//
// So a wrong-looking frame is a *recovery* bug with a component name attached,
// never a hardcoded visual to keep in sync.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ruby/caver/render/gl_pipeline.h"
#include "tools/av_renderer.h"
#include "tools/pod_loader.h"
#include "tools/scene_loader.h"

struct SDL_Window;

namespace caver {
namespace game {

// A `.pod` uploaded once and shared by every object that names it.
struct RenderModel {
    av::PODModel                   pod;
    std::vector<render::GlMesh>    gpu;       // one per pod.meshes (by mesh index)
    std::vector<unsigned int>      textures;  // one per pod.texture_filenames
    std::string                    path;
    float                          feet_offset = 0.0f;
};

// One renderable *level* object. `live` is the object's own av::SceneObject with
// its transform overwritten from the runtime every frame, so moving platforms,
// elevators and script-moved props track the simulation.
struct RenderObject {
    int                 scene_index = -1;
    std::string         identifier;
    av::SceneObject     live;
    const RenderModel*  model = nullptr;   // ModelComponent.Name
    std::vector<render::GlMesh> ground;    // object-embedded GroundMesh meshes
    std::vector<unsigned int> ground_textures;
    unsigned int        background_tex = 0;   // BackgroundComponent texture
    bool                is_portal = false;
    bool                is_dimension = false;
    // The object's animation clock (seconds), from the runtime's render state.
    float               anim_time = 0.0f;
};

// A dynamically placed actor: the hero, and anything spawned through the SCL
// runtime (`Scene.CreateObject`, drops, casts, monsters).
struct RenderActor {
    std::string        key;
    std::string        template_name;
    const RenderModel* model = nullptr;
    // The character animation currently playing, resolved to its own pod. Swordigo
    // stores each lifecycle clip as a separate animation-only .POD named by the
    // KeyframeAnimationComponent (hiro_stand, hiro_run, hiro_jump, …) that the
    // loader merges onto the base model, so switching clip = switching pod.
    std::string        clip;
    const RenderModel* clip_model = nullptr;
    float              clip_time = 0.0f;
    float              pos[3]  = {0.0f, 0.0f, 0.0f};
    float              rot     = 0.0f;   // in-plane rotation (radians)
    float              facing  = 1.0f;   // ±1 horizontal flip
    float              scale   = 1.0f;
    // Animation clock in SECONDS (the runtime's own ObjectRenderState::anim_time).
    // The draw path converts to frames with the pod's own fps — never a constant.
    float              anim_time = 0.0f;
    float              feet_offset = 0.0f;
    float              hitbox_width = 0.0f;
    bool               has_model_y_rotation = false;
    float              model_y_rotation = 0.0f;
    bool               hidden = false;
};

class GameRenderer {
public:
    struct Options {
        int  width  = 1280;
        int  height = 720;
        bool visible = true;
        bool vsync   = true;
        bool postfx  = true;   // HDR + bloom/SSAO/grade pipeline
        // Draw-pass mask, for bisecting a wrong frame down to the pass that
        // produced it. OR of LayerFlag; all passes by default.
        unsigned layers = 0x7f;
        std::string title = "opensw";
    };

    // One bit per draw pass in GameRenderer::render().
    enum LayerFlag : unsigned {
        kLayerBackgrounds = 1u << 0,
        kLayerObjects     = 1u << 1,
        kLayerActors      = 1u << 2,
        kLayerShadows     = 1u << 3,
        kLayerLights      = 1u << 4,
        kLayerWater       = 1u << 5,
        // Object-embedded GroundMesh geometry (its own pass: a level's ground
        // is drawn by the objects that own it, not by a separate system).
        kLayerGround      = 1u << 6,
    };

    struct Stats {
        int draw_calls = 0;
        int objects    = 0;
        int actors     = 0;
        int triangles  = 0;
        double cpu_ms  = 0.0;
    };

    GameRenderer();
    ~GameRenderer();

    GameRenderer(const GameRenderer&) = delete;
    GameRenderer& operator=(const GameRenderer&) = delete;

    // SDL3 window + GL 3.3 core context + av::renderer_init(). Safe to call with
    // visible=false (offscreen rendering for --frames / --shot).
    bool init(const Options& options);
    void shutdown();

    // Build the GPU side of a loaded level. `scene` must already have resolved
    // templates (av::scene_load does that).
    bool build_level(const av::SceneData& scene);

    // Live transform sync for a level object (elevators, script-moved props,
    // animated torches). `scale` is the uniform object scale, `anim_time` the
    // object's animation clock in seconds.
    void sync_object(int scene_index, const float pos[3], float rot, float scale,
                     float anim_time, bool hidden);

    // Per-object diagnostic: what each parsed level object resolved to. Used by
    // `opensw --dump-render` to answer "what is covering the screen" without a
    // debugger.
    void dump_objects() const;

    // Actors. add_actor resolves the archetype's own ModelComponent to a pod.
    int  add_actor(const std::string& key, const av::SceneObject& archetype);
    int  find_actor(const std::string& key) const;
    void clear_actors();
    // `facing` mirrors the actor horizontally, `rot` is the in-plane rotation.
    // `anim_time` is seconds — the pod's own fps turns it into frames.
    void set_actor(int index, const float pos[3], float rot, float facing,
                   float scale, float anim_time, bool hidden);
    void set_actor_anim(int index, float anim_time);
    // Play `clip_name` (the runtime's ObjectRenderState::anim_name) from the
    // archetype's animation pods, at `clip_seconds` into the cycle. An unknown or
    // empty clip falls back to the archetype's own model (the rest pose).
    void set_actor_clip(int index, const std::string& clip_name, float clip_seconds);
    RenderActor*       actor(int index);
    const RenderActor* actor(int index) const;
    int actor_count() const { return static_cast<int>(actors_.size()); }

    // ── window / input ─────────────────────────────────────────────────────
    bool pump_events();                  // false when the user closed the window
    bool quit_requested() const { return quit_; }
    bool key_down(int scancode) const;   // SDL_Scancode, kept as int (no SDL here)
    void set_title(const std::string& title);
    int  width() const { return width_; }
    int  height() const { return height_; }
    bool resized();

    // ── frame ──────────────────────────────────────────────────────────────
    void render(const av::Camera& camera, float time_sec);
    void present();
    // Which draw passes render() is allowed to run (Options::layers).
    void set_layers(unsigned layers) { layers_ = layers; }
    unsigned layers() const { return layers_; }
    // Read the last rendered frame back and write it to disk (.png via
    // SDL3_image, .ppm otherwise). Must be called after render()/present().
    bool save_screenshot(const std::string& path);

    const Stats& stats() const { return stats_; }
    int model_count() const { return static_cast<int>(models_.size()); }
    int texture_count() const { return static_cast<int>(textures_.size()); }
    int object_count() const { return static_cast<int>(objects_.size()); }
    const std::string& last_error() const { return error_; }
    const std::vector<std::string>& warnings() const { return warnings_; }

    // Global lighting knobs, already av::'s own defaults; exposed so the shell
    // can print what the frame is actually lit by.
    void set_exposure(float exposure) { exposure_ = exposure; }
    float exposure() const { return exposure_; }

private:
    // ── resource loading ───────────────────────────────────────────────────
    const RenderModel* model_for(const std::string& mesh_name);
    const RenderModel* model_from_path(const std::string& pod_path,
                                       const std::string& merge_hint);
    unsigned int texture_for(const std::string& texture_name,
                             const std::string& owner_path);
    unsigned int load_texture_file(const std::string& path);
    unsigned int upload_rgba(const uint8_t* pixels, int w, int h);

    // ── draw helpers ───────────────────────────────────────────────────────
    void draw_model(const RenderModel& model, const float model_matrix[16],
                    float anim_seconds);
    void draw_object(const RenderObject& object, float anim_frame);
    void draw_actor(const RenderActor& actor_);
    void draw_waters(float time_sec);
    void draw_shadows();
    // BackgroundComponent pass: drawn last, in screen space, exactly where the
    // world left the depth buffer untouched (Caver::Scene::Draw order).
    void draw_backgrounds(const av::Camera& camera);
    // Lights, from the scene's LightComponent set: type 1 ambient, type 2
    // directional, type 3 point (the Caver::PointLightManager set) and type 4
    // the LightOverlay veil. Fills point_lights_/veil_alpha_ and uploads the
    // shader uniforms; call before begin_scene().
    void gather_lights(float time_sec);
    // The fire/light glows (billboards), queued during the frame.
    void queue_light_glows(float time_sec);
    void build_quad_mesh();
    unsigned int ensure_white_texture();
    unsigned int white_texture_ = 0;

    // ── GL state ───────────────────────────────────────────────────────────
    // The draw path: caver's own GL 3.3 core shader pipeline (ported from the
    // Ruby GG viewport shader). av:: is used only for DATA here — pod parsing,
    // scene loading, texture decoding, matrix math.
    render::GlPipeline pipeline_;
    // Unit quad (pos/normal/uv, 2 triangles) for backgrounds and water sheets.
    render::GlMesh     quad_mesh_;
    SDL_Window* window_ = nullptr;
    void*       gl_context_ = nullptr;
    // Pixel size of the drawable (NOT the logical window size): on a HiDPI
    // display these differ by the scale factor, and the scene framebuffer has
    // to track them or the viewport clips the scene into a corner.
    int         width_ = 0, height_ = 0;
    int         fbo_width_ = 0, fbo_height_ = 0;
    bool        visible_ = true;
    bool        postfx_ = true;
    unsigned    layers_ = 0x7f;

    // ── input ──────────────────────────────────────────────────────────────
    std::vector<uint8_t> keys_;
    bool quit_ = false;
    bool resized_ = false;

    // ── resources ──────────────────────────────────────────────────────────
    std::string scene_path_;
    std::string scene_dir_;
    std::unordered_map<std::string, RenderModel>            models_;    // by mesh name
    std::unordered_map<std::string, unsigned int>           textures_;  // by file path
    // GL texture id -> width/height, so a background quad can preserve its own
    // sheet's aspect ratio instead of stretching to the window.
    std::unordered_map<unsigned int, float>                 texture_aspects_;
    std::vector<RenderObject>                               objects_;
    std::vector<RenderActor>                                actors_;
    std::unordered_map<int, size_t>                          by_scene_index_;

    // ── parsed render-side scene state (copied from the level) ─────────────
    std::vector<av::SceneData::SceneWater>  waters_;
    std::vector<av::SceneData::SceneLight>  lights_;
    std::vector<av::SceneData::SceneShadow> shadows_;
    std::vector<av::SceneData::SceneOverlay> overlays_;
    float clear_color_[3] = {0.06f, 0.07f, 0.10f};

    Stats       stats_;
    float       exposure_ = 1.0f;
    float       flicker_seed_ = 0.0f;
    // This frame's point lights (nearest 16 to the camera), shared by the
    // lighting pass and the LightOverlay veil.
    std::vector<render::PointLight> point_lights_;
    float       veil_alpha_ = 0.0f;   // 0.5 * the scene's overlay (type 4) intensity
    // Camera focus of the frame being drawn (the engine's own look-at point) —
    // used to pick the nearest lights rather than the first sixteen in the file.
    float       focus_[3] = {0.0f, 0.0f, 0.0f};
    std::string error_;
    std::vector<std::string> warnings_;
};

} // namespace game
} // namespace caver
