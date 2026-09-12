#pragma once
// ============================================================================
// particle_engine.h — Dynamic particle simulation & batch renderer
//   Features 5 emitter modes (radial burst, explosive shrapnel, continuous
//   area volume, ambient floating spores, directional cone flame) + fire
//   torches/braziers with camera-facing billboards, gravity, rotation, and alpha fadeout.
// ============================================================================

#include <QOpenGLFunctions_3_3_Compatibility>
#include <vector>
#include <string>
#include "tools/scene_loader.h"

namespace ruby::render {

struct Particle {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;
    float base_a = 1.0f;
    float alpha = 1.0f;
    float rotation = 0.0f;
    float rot_speed = 0.0f;
    float age = 0.0f;
    float max_age = 1.0f;
    float cur_size = 0.0f;
    float target_size = 10.0f;
    float base_scale = 0.25f; // size scale floor during fadeout
    int   type_idx = 0;
};

struct EmitterDef {
    int   object_id = -1;
    int   emitter_type = 5; // 1=Radial, 2=Explosion, 3=AreaVolume, 4=AmbientSparks, 5=Cone/Stream
    bool  is_fire = false;
    bool  is_portal = false;
    float world_pos[3] = {0.0f, 0.0f, 0.0f};
    float base_color[4] = {1.0f, 0.6f, 0.2f, 1.0f};
    float hue_var = 0.05f;
    float sat_var = 0.10f;
    float light_var = 0.10f;
    float origin_offset[3] = {0.0f, 0.0f, 0.0f};
    float gravity[3] = {0.0f, 0.0f, 0.0f};
    float fire_interval = 0.08f;
    float fire_max_age = 0.65f;
    float fire_spread[3] = {4.0f, 2.0f, 4.0f};
    float emit_accumulator = 0.0f;
    bool  one_shot_done = false;
    int   max_particles = 120;

    // Vanilla light flicker dynamics (Caver::FireEmitterComponent)
    float light_intensity_current = 1.0f;
    float light_intensity_target  = 1.0f;
    float light_slew_speed        = 0.5f;

    std::vector<float> params;
    float get_param(size_t idx, float def_val) const {
        return idx < params.size() ? params[idx] : def_val;
    }

    std::vector<Particle> particles;
};

class ParticleEngine : protected QOpenGLFunctions_3_3_Compatibility {
public:
    ParticleEngine() = default;
    ~ParticleEngine();

    ParticleEngine(const ParticleEngine&) = delete;
    ParticleEngine& operator=(const ParticleEngine&) = delete;

    /// Compile GLSL 330 particle shader and create dynamic buffers. Call in initializeGL().
    bool init();

    /// Free GL resources.
    void shutdown();

    /// Synchronize particle emitters with current scene objects.
    void sync_scene(const av::SceneData& scene);

    /// Clear all active emitters and particles.
    void clear();

    /// Update physics and render all particle billboards.
    void update_and_render(float dt,
                           const float view[16],
                           const float view_proj[16],
                           GLuint particle_texture = 0);

    bool ready() const { return m_prog != 0; }
    const std::vector<EmitterDef>& emitters() const { return m_emitters; }

private:
    void update_emitter(EmitterDef& em, float dt);
    void spawn_particle(EmitterDef& em, const Particle& p);

    std::vector<EmitterDef> m_emitters;

    GLuint m_prog = 0;
    GLuint m_vao  = 0;
    GLuint m_vbo  = 0;
    GLuint m_ebo  = 0;

    GLint m_loc_viewproj = -1;
    GLint m_loc_texture  = -1;
    GLint m_loc_has_tex  = -1;

    struct Vertex {
        float pos[3];
        float uv[2];
        uint8_t color[4];
    };

    std::vector<Vertex>   m_vertex_data;
    std::vector<uint16_t> m_index_data;
};

} // namespace ruby::render
