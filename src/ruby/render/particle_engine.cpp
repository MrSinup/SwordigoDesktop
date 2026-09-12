// ============================================================================
// particle_engine.cpp — Dynamic particle simulation & batch renderer
// ============================================================================

#include "particle_engine.h"
#include "platform/protobuf_reader.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <random>

namespace ruby::render {

static const char* k_particle_vs = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform mat4 uViewProj;

out vec4 vColor;
out vec2 vUV;

void main() {
    vColor = aColor;
    vUV = aUV;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)GLSL";

static const char* k_particle_fs = R"GLSL(
#version 330 core
in vec4 vColor;
in vec2 vUV;

uniform sampler2D uTexture;
uniform int       uHasTex;

out vec4 FragColor;

void main() {
    vec4 tex = (uHasTex != 0) ? texture(uTexture, vUV) : vec4(1.0);

    // Procedural soft particle circular falloff if no custom texture
    if (uHasTex == 0) {
        float d = length(vUV - 0.5) * 2.0;
        if (d > 1.0) discard;
        float falloff = 1.0 - smoothstep(0.1, 1.0, d);
        tex = vec4(1.0, 1.0, 1.0, falloff);
    }

    vec4 finalCol = vColor * tex;
    if (finalCol.a < 0.01) discard;

    FragColor = finalCol;
}
)GLSL";

static float rand01() {
    static std::mt19937 rng(1337);
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng);
}

static void rgb_to_hsl(float r, float g, float b, float& h, float& s, float& l) {
    float max_c = std::max({r, g, b});
    float min_c = std::min({r, g, b});
    float delta = max_c - min_c;
    l = (max_c + min_c) * 0.5f;
    if (delta < 1e-4f) {
        h = 0.0f;
        s = 0.0f;
        return;
    }
    s = (l > 0.5f) ? delta / (2.0f - max_c - min_c) : delta / (max_c + min_c);
    if (max_c == r) h = (g - b) / delta + (g < b ? 6.0f : 0.0f);
    else if (max_c == g) h = (b - r) / delta + 2.0f;
    else h = (r - g) / delta + 4.0f;
    h /= 6.0f;
}

static float hue2rgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

static void hsl_to_rgb(float h, float s, float l, float& r, float& g, float& b) {
    if (s < 1e-4f) {
        r = g = b = l;
        return;
    }
    float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    r = hue2rgb(p, q, h + 1.0f / 3.0f);
    g = hue2rgb(p, q, h);
    b = hue2rgb(p, q, h - 1.0f / 3.0f);
}

static void jitter_color(const float base[4], float hue_var, float sat_var, float light_var, float out_col[4]) {
    float h, s, l;
    rgb_to_hsl(base[0], base[1], base[2], h, s, l);
    h = std::fmod(h + (rand01() * 2.0f - 1.0f) * hue_var + 1.0f, 1.0f);
    s = std::clamp(s + (rand01() * 2.0f - 1.0f) * sat_var, 0.0f, 1.0f);
    l = std::clamp(l + (rand01() * 2.0f - 1.0f) * light_var, 0.0f, 1.0f);
    hsl_to_rgb(h, s, l, out_col[0], out_col[1], out_col[2]);
    out_col[3] = base[3];
}

static bool parse_float_color(const std::string& bytes, float out[4]) {
    if (bytes.empty()) return false;
    try {
        proto::Reader reader(bytes);
        proto::Field f;
        int count = 0;
        float c[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        while (reader.read_field(f)) {
            if (f.wire_type == proto::WIRE_I32 && f.field_number >= 1 && f.field_number <= 4) {
                if (std::isfinite(f.float_val)) {
                    c[f.field_number - 1] = f.float_val;
                    count++;
                }
            }
        }
        if (count >= 3) {
            for (int i = 0; i < 4; ++i) out[i] = c[i];
            return true;
        }
    } catch (...) {}
    return false;
}

static bool parse_vector3(const std::string& bytes, float out[3]) {
    if (bytes.empty()) return false;
    try {
        proto::Reader reader(bytes);
        proto::Field f;
        int count = 0;
        float v[3] = {0.0f, 0.0f, 0.0f};
        while (reader.read_field(f)) {
            if (f.wire_type == proto::WIRE_I32 && f.field_number >= 1 && f.field_number <= 3) {
                if (std::isfinite(f.float_val)) {
                    v[f.field_number - 1] = f.float_val;
                    count++;
                }
            }
        }
        if (count > 0) {
            for (int i = 0; i < 3; ++i) out[i] = v[i];
            return true;
        }
    } catch (...) {}
    return false;
}

static void parse_particle_emitter(const std::string& bytes, EmitterDef& em) {
    if (bytes.empty()) return;
    try {
        proto::Reader reader(bytes);
        proto::Field f;
        while (reader.read_field(f)) {
            if (f.field_number == 1 && f.wire_type == proto::WIRE_VARINT) {
                em.emitter_type = static_cast<int>(f.varint_val);
            } else if (f.field_number == 2 && f.wire_type == proto::WIRE_LEN) {
                parse_float_color(f.bytes_val, em.base_color);
            } else if (f.field_number == 3) {
                if (f.wire_type == proto::WIRE_I32) {
                    if (std::isfinite(f.float_val)) em.params.push_back(f.float_val);
                } else if (f.wire_type == proto::WIRE_LEN) {
                    const size_t n = f.bytes_val.size() / 4;
                    for (size_t i = 0; i < n; ++i) {
                        float val = 0.0f;
                        std::memcpy(&val, f.bytes_val.data() + i * 4, 4);
                        if (std::isfinite(val)) em.params.push_back(val);
                    }
                }
            } else if (f.field_number == 4 && f.wire_type == proto::WIRE_I32) {
                if (std::isfinite(f.float_val)) em.hue_var = f.float_val;
            } else if (f.field_number == 5 && f.wire_type == proto::WIRE_I32) {
                if (std::isfinite(f.float_val)) em.sat_var = f.float_val;
            } else if (f.field_number == 6 && f.wire_type == proto::WIRE_I32) {
                if (std::isfinite(f.float_val)) em.light_var = f.float_val;
            } else if (f.field_number == 7 && f.wire_type == proto::WIRE_LEN) {
                parse_vector3(f.bytes_val, em.origin_offset);
            }
        }
    } catch (...) {}
}

ParticleEngine::~ParticleEngine() {
    shutdown();
}

bool ParticleEngine::init() {
    // Bail out rather than call through a null table (offscreen / core-profile
    // contexts cannot supply the 3.3 compatibility entry points).
    if (!initializeOpenGLFunctions()) {
        fprintf(stderr, "[ParticleEngine] GL 3.3 compatibility table unavailable — particles disabled.\n");
        return false;
    }

    auto compile_shader = [this](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[512];
            glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
            fprintf(stderr, "[ParticleEngine] Shader error: %s\n", buf);
            glDeleteShader(s);
            return 0u;
        }
        return s;
    };

    GLuint vs = compile_shader(GL_VERTEX_SHADER, k_particle_vs);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, k_particle_fs);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    m_prog = glCreateProgram();
    glAttachShader(m_prog, vs);
    glAttachShader(m_prog, fs);
    glLinkProgram(m_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(m_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(m_prog, sizeof(buf), nullptr, buf);
        fprintf(stderr, "[ParticleEngine] Program link error: %s\n", buf);
        glDeleteProgram(m_prog);
        m_prog = 0;
        return false;
    }

    m_loc_viewproj = glGetUniformLocation(m_prog, "uViewProj");
    m_loc_texture  = glGetUniformLocation(m_prog, "uTexture");
    m_loc_has_tex  = glGetUniformLocation(m_prog, "uHasTex");

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);

    // layout 0: pos (vec3) -> stride 24, offset 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    // layout 1: uv (vec2) -> stride 24, offset 12
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 24, (void*)12);
    // layout 2: color (vec4) -> stride 24, offset 20, GL_UNSIGNED_BYTE normalized
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 24, (void*)20);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    return true;
}

void ParticleEngine::shutdown() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_prog) { glDeleteProgram(m_prog); m_prog = 0; }
    m_emitters.clear();
}

void ParticleEngine::clear() {
    for (auto& em : m_emitters) {
        em.particles.clear();
        em.emit_accumulator = 0.0f;
        em.one_shot_done = false;
    }
}

void ParticleEngine::sync_scene(const av::SceneData& scene) {
    std::vector<EmitterDef> new_emitters;

    for (size_t oi = 0; oi < scene.objects.size(); ++oi) {
        const auto& obj = scene.objects[oi];
        if (obj.hidden) continue;

        const auto& comps = obj.resolved_components.empty()
                                ? obj.components
                                : obj.resolved_components;

        bool has_fire = false;
        bool has_particle = false;
        EmitterDef em;
        em.object_id = static_cast<int>(oi);
        em.world_pos[0] = obj.pos_x;
        em.world_pos[1] = obj.pos_y;
        em.world_pos[2] = obj.pos_z;

        for (const auto& c : comps) {
            const bool is_fire_comp = (c.type_name.find("FireEmitter") != std::string::npos) ||
                                      (c.payload_field == 253) || (c.type_id == 253);
            if (is_fire_comp) {
                has_fire = true;
                em.is_fire = true;
                em.base_color[0] = 1.0f; em.base_color[1] = 0.60f; em.base_color[2] = 0.20f; em.base_color[3] = 0.95f;
                em.fire_interval = 0.045f;
                em.fire_max_age = 0.65f;
                em.fire_spread[0] = 2.5f; em.fire_spread[1] = 1.5f; em.fire_spread[2] = 2.5f;

                for (const auto& f : av::scene_component_fields(c)) {
                    if ((f.name == "Color" || f.field_number == 5) && !f.bytes_value.empty()) {
                        parse_float_color(f.bytes_value, em.base_color);
                    } else if ((f.name == "Origin" || f.field_number == 2) && !f.bytes_value.empty()) {
                        float org[3] = {0.0f, 0.0f, 0.0f};
                        if (parse_vector3(f.bytes_value, org)) {
                            em.origin_offset[0] += org[0];
                            em.origin_offset[1] += org[1];
                            em.origin_offset[2] += org[2];
                        }
                    } else if ((f.name == "ParticleInterval" || f.field_number == 6) && f.float_value > 0.0f) {
                        em.fire_interval = std::max(0.01f, f.float_value);
                    } else if ((f.name == "ParticleMaxAge" || f.field_number == 7) && f.float_value > 0.0f) {
                        em.fire_max_age = std::max(0.05f, f.float_value);
                    } else if ((f.name == "ParticleSpread" || f.field_number == 8) && !f.bytes_value.empty()) {
                        float sp[3] = {0.0f, 0.0f, 0.0f};
                        if (parse_vector3(f.bytes_value, sp)) {
                            for (int k = 0; k < 3; ++k) em.fire_spread[k] = std::fabs(sp[k]);
                        }
                    }
                }
            }

            const bool is_particle_comp = (c.type_name.find("ParticleEmitter") != std::string::npos) ||
                                          (c.payload_field == 250) || (c.type_id == 250);
            if (is_particle_comp) {
                has_particle = true;
                for (const auto& f : av::scene_component_fields(c)) {
                    if ((f.name == "MaxParticles" || f.field_number == 4) && f.varint_value > 0) {
                        em.max_particles = std::min(300, static_cast<int>(f.varint_value));
                    } else if ((f.name == "Emitter" || f.field_number == 7) && !f.bytes_value.empty()) {
                        parse_particle_emitter(f.bytes_value, em);
                    } else if ((f.name == "Gravity" || f.field_number == 9) && !f.bytes_value.empty()) {
                        parse_vector3(f.bytes_value, em.gravity);
                    }
                }
            }
        }

        // Auto-detect torch / fire objects if not explicitly flagged
        if (!has_fire && !has_particle) {
            std::string low_name = obj.name;
            for (char& ch : low_name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            std::string low_tpl = obj.template_name;
            for (char& ch : low_tpl) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

            if (low_name.find("torch") != std::string::npos ||
                low_name.find("brazier") != std::string::npos ||
                low_name.find("campfire") != std::string::npos ||
                low_tpl.find("torch") != std::string::npos ||
                low_tpl.find("brazier") != std::string::npos ||
                low_tpl.find("campfire") != std::string::npos) {
                has_fire = true;
                em.is_fire = true;
                em.base_color[0] = 1.0f; em.base_color[1] = 0.60f; em.base_color[2] = 0.20f; em.base_color[3] = 0.95f;
                em.origin_offset[1] = 20.0f * std::fabs(obj.scale_y);
                em.fire_interval = 0.045f;
                em.fire_max_age = 0.65f;
                em.fire_spread[0] = 2.5f; em.fire_spread[1] = 1.5f; em.fire_spread[2] = 2.5f;
            }
        }

        // If torch / fire object has 0 origin_offset, apply default height offset to put flame at the cup
        if (em.is_fire && em.origin_offset[0] == 0.0f && em.origin_offset[1] == 0.0f && em.origin_offset[2] == 0.0f) {
            em.origin_offset[1] = 20.0f * std::fabs(obj.scale_y);
        }

        // Auto-detect portal frame objects (ONLY portals with the portal 3D model/mesh get effects!)
        // Non-portal frame portals (scene-edge based transition triggers with no mesh) get NO effects!
        bool is_portal_frame = false;
        if (!obj.mesh_name.empty()) {
            std::string low_mesh = obj.mesh_name;
            for (char& ch : low_mesh) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            std::string low_tpl = obj.template_name;
            for (char& ch : low_tpl) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

            if (low_mesh.find("portal") != std::string::npos || low_tpl.find("portal") != std::string::npos) {
                is_portal_frame = true;
                em.is_portal = true;
                em.is_fire = false;
                em.base_color[0] = 0.78f; em.base_color[1] = 0.88f; em.base_color[2] = 1.0f; em.base_color[3] = 0.85f; // bluish-white
                em.origin_offset[1] = 28.0f * std::fabs(obj.scale_y); // center of the portal arch aperture
                em.max_particles = 40;
            }
        }

        if (has_fire || has_particle || is_portal_frame) {
            new_emitters.push_back(std::move(em));
        }
    }

    // Preserve existing active particles across re-syncs
    for (auto& ne : new_emitters) {
        for (const auto& oe : m_emitters) {
            if (oe.object_id == ne.object_id) {
                ne.particles = oe.particles;
                ne.emit_accumulator = oe.emit_accumulator;
                ne.one_shot_done = oe.one_shot_done;
                break;
            }
        }
    }

    m_emitters = std::move(new_emitters);
}

void ParticleEngine::spawn_particle(EmitterDef& em, const Particle& p) {
    if ((int)em.particles.size() >= em.max_particles) return;
    em.particles.push_back(p);
}

void ParticleEngine::update_emitter(EmitterDef& em, float dt) {
    if (em.is_fire) {
        // Vanilla light intensity flicker (00000000002C9D60__Update.c lines 158-179)
        float diff = em.light_intensity_target - em.light_intensity_current;
        float step = em.light_slew_speed * dt;
        if (std::abs(diff) >= step + 0.001f) {
            em.light_intensity_current += (diff > 0.0f ? 1.0f : -1.0f) * step;
        } else {
            em.light_intensity_current = em.light_intensity_target;
            em.light_intensity_target = 0.65f + rand01() * 0.35f;
            em.light_slew_speed = 0.5f + rand01() * 0.5f;
        }

        em.emit_accumulator += dt;
        const float base_interval = em.get_param(11, em.fire_interval);
        const float interval = std::clamp(base_interval, 0.015f, 0.15f);

        while (em.emit_accumulator > interval) {
            em.emit_accumulator -= interval;
            if ((int)em.particles.size() >= em.max_particles) break;

            // JS Editor cone stream angle: central angle (default 90 deg = +Y) + spread (default 30 deg)
            const float center_deg = em.get_param(0, 90.0f);
            const float spread_deg = em.get_param(1, 30.0f);
            const float angle_rad = (center_deg + (rand01() - 0.5f) * spread_deg) * (3.14159265f / 180.0f);
            const float dir_x = std::cos(angle_rad);
            const float dir_y = std::sin(angle_rad);

            // Muzzle speed: default 48.0 (p5 with p6 variance)
            const float speed_base = em.get_param(5, 48.0f);
            const float speed_var  = em.get_param(6, 0.15f);
            const float speed = speed_base * (1.0f + (rand01() * 2.0f - 1.0f) * speed_var);

            // Spawn jitter around wick / flame cup
            const float sx = em.fire_spread[0] * (rand01() * 2.0f - 1.0f);
            const float sy = em.fire_spread[1] * (rand01() * 2.0f - 1.0f);
            const float sz = em.fire_spread[2] * (rand01() * 2.0f - 1.0f);

            Particle p;
            // Sub-frame delta for silky smooth particle emission without banding
            const float h = rand01() / 60.0f;
            p.x = em.world_pos[0] + em.origin_offset[0] + sx + dir_x * speed * h;
            p.y = em.world_pos[1] + em.origin_offset[1] + sy + dir_y * speed * h;
            p.z = em.world_pos[2] + em.origin_offset[2] + sz;

            // Velocity combining JS Editor directional cone + vanilla inward curl column
            p.vx = dir_x * speed - 1.2f * sx;
            p.vy = dir_y * speed;
            p.vz = (rand01() - 0.5f) * (speed * 0.22f) - 1.2f * sz;

            // Color: JS Editor GS() HSL jittering
            float jittered[4];
            jitter_color(em.base_color, em.hue_var, em.sat_var, em.light_var, jittered);
            p.r = jittered[0];
            p.g = jittered[1];
            p.b = jittered[2];
            p.base_a = jittered[3];
            p.alpha = 1.0f;

            p.rotation = rand01() * 6.2831853f;
            p.rot_speed = (rand01() - 0.5f) * 0.3f * 6.2831853f; // JS editor rotSpeed: _t() * 0.3 * 2pi

            const float max_age_base = em.get_param(8, em.fire_max_age);
            const float max_age_var  = em.get_param(14, 0.15f);
            p.max_age = std::max(0.1f, max_age_base * (1.0f + (rand01() * 2.0f - 1.0f) * max_age_var));
            p.age = p.max_age * em.get_param(9, 0.0f);

            // Size: JS editor typeSize * p12 * (1 + rand * p13)
            const float size_mult = em.get_param(12, 1.0f);
            const float size_var  = em.get_param(13, 0.2f);
            p.target_size = (7.5f + rand01() * 3.5f) * size_mult * (1.0f + (rand01() * 2.0f - 1.0f) * size_var);
            p.cur_size = 0.0f;
            p.base_scale = 0.25f;

            spawn_particle(em, p);
        }
    } else if (em.is_portal) {
        // Portal frame particle emission: delicate bluish-white sparkle motes
        // drifting and swirling upwards inside the stone arch aperture
        em.emit_accumulator += dt;
        const float interval = 0.08f;
        while (em.emit_accumulator > interval) {
            em.emit_accumulator -= interval;
            if ((int)em.particles.size() >= em.max_particles) break;

            // Aperture oval spawn: width ~ 9.5 units, height ~ 17.5 units
            const float angle = rand01() * 6.2831853f;
            const float rad = std::sqrt(rand01());
            const float sx = std::cos(angle) * rad * 9.5f;
            const float sy = std::sin(angle) * rad * 17.5f;
            const float sz = (rand01() * 2.0f - 1.0f) * 2.5f;

            Particle p;
            p.x = em.world_pos[0] + em.origin_offset[0] + sx;
            p.y = em.world_pos[1] + em.origin_offset[1] + sy;
            p.z = em.world_pos[2] + em.origin_offset[2] + sz;

            // Gentle drifting upward float with subtle sway
            p.vx = (rand01() * 2.0f - 1.0f) * 3.0f - sx * 0.15f;
            p.vy = 12.0f + rand01() * 8.0f; // upward floating motes
            p.vz = (rand01() * 2.0f - 1.0f) * 3.0f;

            // Bluish-white sparkle motes
            const float rnd = rand01();
            p.r = 0.72f + rnd * 0.18f;
            p.g = 0.84f + rnd * 0.14f;
            p.b = 1.0f;
            p.base_a = 0.60f + rand01() * 0.30f;
            p.alpha = 1.0f;

            p.rotation = rand01() * 6.2831853f;
            p.rot_speed = (rand01() - 0.5f) * 2.0f;
            p.max_age = 1.1f + rand01() * 0.5f;
            p.age = 0.0f;

            p.target_size = 3.5f + rand01() * 2.5f; // small delicate motes
            p.cur_size = 0.0f;
            p.base_scale = 0.2f;

            spawn_particle(em, p);
        }
    } else {
        // Standard particle modes (1..5) matching JS editor HS.emit()
        switch (em.emitter_type) {
            case 1: {
                if (!em.one_shot_done) {
                    em.one_shot_done = true;
                    for (int t = 0; t < 6; ++t) {
                        const float n = (rand01() + t) / 6.0f * 6.2831853f;
                        const float s = em.get_param(0, 20.0f) * (0.5f + rand01() * 0.5f);
                        const float sz = std::max(em.get_param(2, 20.0f), em.get_param(3, 20.0f));
                        Particle p;
                        p.x = em.world_pos[0] + em.origin_offset[0] + std::cos(n) * s;
                        p.y = em.world_pos[1] + em.origin_offset[1] + std::sin(n) * s;
                        p.z = em.world_pos[2] + em.origin_offset[2];
                        p.vx = std::cos(n) * 15.0f;
                        p.vy = std::sin(n) * 15.0f;
                        p.vz = 0.0f;
                        float jittered[4];
                        jitter_color(em.base_color, em.hue_var, em.sat_var, em.light_var, jittered);
                        p.r = jittered[0]; p.g = jittered[1]; p.b = jittered[2]; p.base_a = jittered[3];
                        p.rotation = n;
                        p.rot_speed = (rand01() - 0.5f) * 2.0f;
                        p.max_age = em.get_param(1, 0.6f);
                        p.target_size = sz;
                        p.cur_size = (rand01() * 0.5f + 0.5f) * sz;
                        p.base_scale = 0.25f;
                        spawn_particle(em, p);
                    }
                }
                break;
            }
            case 2: {
                if (!em.one_shot_done) {
                    em.one_shot_done = true;
                    for (int t = 0; t < 7; ++t) {
                        const float n = (rand01() + t + 1) / 7.0f * 6.2831853f;
                        const float s = em.get_param(0, 100.0f);
                        Particle p;
                        p.x = em.world_pos[0] + em.origin_offset[0];
                        p.y = em.world_pos[1] + em.origin_offset[1];
                        p.z = em.world_pos[2] + em.origin_offset[2];
                        p.vx = std::cos(n) * s;
                        p.vy = std::sin(n) * s;
                        p.vz = (rand01() - 0.5f) * (s * 0.3f);
                        float jittered[4];
                        jitter_color(em.base_color, em.hue_var, em.sat_var, em.light_var, jittered);
                        p.r = jittered[0]; p.g = jittered[1]; p.b = jittered[2]; p.base_a = jittered[3];
                        p.rotation = rand01() * 6.2831853f;
                        p.rot_speed = (rand01() - 0.5f) * 4.0f;
                        p.max_age = em.get_param(1, 0.6f) * (0.5f + rand01() * 0.5f);
                        p.target_size = std::max(em.get_param(2, 8.0f), em.get_param(3, 8.0f));
                        p.cur_size = 0.0f;
                        p.base_scale = 0.25f;
                        spawn_particle(em, p);
                    }
                }
                break;
            }
            case 3: {
                em.emit_accumulator += dt;
                const float t_int = std::max(em.get_param(0, 0.08f), 0.01f);
                while (em.emit_accumulator > t_int) {
                    em.emit_accumulator -= t_int;
                    if ((int)em.particles.size() >= em.max_particles) break;
                    const float n = rand01() * 6.2831853f;
                    const float s = em.get_param(1, 1.0f);
                    const float r = em.get_param(3, 10.0f) / std::max(s, 0.05f);
                    const float o = s * (0.8f + rand01() * 0.2f);
                    Particle p;
                    p.x = em.world_pos[0] + em.origin_offset[0] + std::cos(n) * r * s;
                    p.y = em.world_pos[1] + em.origin_offset[1] + std::sin(n) * r * s;
                    p.z = em.world_pos[2] + em.origin_offset[2] + (rand01() - 0.5f) * 4.0f;
                    p.vx = -std::cos(n) * r;
                    p.vy = -std::sin(n) * r;
                    p.vz = 0.0f;
                    float jittered[4];
                    jitter_color(em.base_color, em.hue_var, em.sat_var, em.light_var, jittered);
                    p.r = jittered[0]; p.g = jittered[1]; p.b = jittered[2]; p.base_a = jittered[3];
                    p.rotation = rand01() * 6.2831853f;
                    p.rot_speed = (rand01() - 0.5f) * 3.0f;
                    p.max_age = o;
                    p.target_size = 8.0f * em.get_param(2, 1.0f);
                    p.cur_size = 0.0f;
                    p.base_scale = 0.25f;
                    spawn_particle(em, p);
                }
                break;
            }
            case 4: {
                em.emit_accumulator += dt;
                const float t_int = std::max(em.get_param(0, 0.08f), 0.01f);
                while (em.emit_accumulator > t_int) {
                    em.emit_accumulator -= t_int;
                    if ((int)em.particles.size() >= em.max_particles) break;
                    const float n = rand01() * 6.2831853f;
                    const float s = em.get_param(1, 0.8f);
                    const float r = em.get_param(3, 20.0f) / std::max(s, 0.05f);
                    Particle p;
                    p.x = em.world_pos[0] + em.origin_offset[0];
                    p.y = em.world_pos[1] + em.origin_offset[1];
                    p.z = em.world_pos[2] + em.origin_offset[2];
                    p.vx = std::cos(n) * r;
                    p.vy = std::sin(n) * r;
                    p.vz = (rand01() - 0.5f) * 6.0f;
                    float jittered[4];
                    jitter_color(em.base_color, em.hue_var, em.sat_var, em.light_var, jittered);
                    p.r = jittered[0]; p.g = jittered[1]; p.b = jittered[2]; p.base_a = jittered[3];
                    p.rotation = rand01() * 6.2831853f;
                    p.rot_speed = (rand01() - 0.5f) * 3.0f;
                    p.max_age = s;
                    p.target_size = 8.0f;
                    p.cur_size = 0.0f;
                    p.base_scale = 0.25f;
                    spawn_particle(em, p);
                }
                break;
            }
            case 5:
            default: {
                em.emit_accumulator += dt;
                const float t_int = std::max(em.get_param(11, 0.1f), 0.005f);
                while (em.emit_accumulator > t_int) {
                    em.emit_accumulator -= t_int;
                    if ((int)em.particles.size() >= em.max_particles) break;
                    const float n = (em.get_param(0, 90.0f) + (rand01() - 0.5f) * em.get_param(1, 30.0f)) * (3.14159265f / 180.0f);
                    const float s = std::cos(n);
                    const float r = std::sin(n);
                    const float o = em.get_param(5, 50.0f) * (1.0f + (rand01() * 2.0f - 1.0f) * em.get_param(6, 0.0f));
                    float a = 0.0f, c = 0.0f;
                    if (em.get_param(2, 0.0f) > 0.001f) {
                        const float g = rand01() * em.get_param(2, 0.0f);
                        a += s * g; c += r * g;
                    }
                    if (em.get_param(3, 0.0f) > 0.001f) a += (rand01() - 0.5f) * em.get_param(3, 0.0f);
                    if (em.get_param(4, 0.0f) > 0.001f) c += (rand01() - 0.5f) * em.get_param(4, 0.0f);
                    const float l = std::max(em.get_param(8, 1.0f) + (rand01() * 2.0f - 1.0f) * em.get_param(14, 0.0f), 0.05f);
                    const float h = rand01() / 60.0f;
                    const float u = s * o;
                    const float d = r * o;
                    Particle p;
                    p.x = em.world_pos[0] + em.origin_offset[0] + a + u * h;
                    p.y = em.world_pos[1] + em.origin_offset[1] + c + d * h;
                    p.z = em.world_pos[2] + em.origin_offset[2] + (rand01() - 0.5f) * 2.0f;
                    p.vx = u;
                    p.vy = d;
                    p.vz = (rand01() - 0.5f) * (o * 0.15f);
                    float jittered[4];
                    jitter_color(em.base_color, em.hue_var, em.sat_var, em.light_var, jittered);
                    p.r = jittered[0]; p.g = jittered[1]; p.b = jittered[2]; p.base_a = jittered[3];
                    p.rotation = rand01() * 6.2831853f;
                    p.rot_speed = (rand01() - 0.5f) * 0.3f * 6.2831853f;
                    p.max_age = l;
                    p.age = l * em.get_param(9, 0.0f);
                    p.target_size = 10.0f * em.get_param(12, 1.0f) * (1.0f + (rand01() * 2.0f - 1.0f) * em.get_param(13, 0.0f));
                    p.cur_size = 0.0f;
                    p.base_scale = 0.25f;
                    spawn_particle(em, p);
                }
                break;
            }
        }
    }

    // Step physics for active particles
    std::vector<Particle> alive;
    alive.reserve(em.particles.size());

    for (auto& p : em.particles) {
        p.age += dt;
        if (p.age >= p.max_age - 0.001f) continue;

        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.z += p.vz * dt;

        p.vx += em.gravity[0] * dt;
        p.vy += em.gravity[1] * dt;
        p.vz += em.gravity[2] * dt;

        p.rotation += p.rot_speed * dt;

        // Vanilla / JS Editor 6x pop-in growth
        if (p.cur_size < p.target_size) {
            p.cur_size = std::min(p.target_size, p.cur_size + p.target_size * 6.0f * dt);
        }

        // Vanilla / JS Editor fadeout alpha & scaling
        const float s = p.age / p.max_age;
        if (s > 0.5f) {
            p.alpha = std::max(0.0f, 1.0f - (s - 0.5f) * 2.0f);
            p.cur_size = p.target_size * (p.base_scale + p.alpha * (1.0f - p.base_scale));
        } else {
            p.alpha = 1.0f;
        }

        alive.push_back(p);
    }

    em.particles = std::move(alive);
}

void ParticleEngine::update_and_render(float dt,
                                       const float view[16],
                                       const float view_proj[16],
                                       GLuint particle_texture) {
    if (!m_prog || !m_vao || m_emitters.empty()) return;

    // Update emitters and particles
    for (auto& em : m_emitters) {
        update_emitter(em, dt);
    }

    // Camera right and up vectors from view matrix
    const float right[3] = { view[0], view[4], view[8]  };
    const float up[3]    = { view[1], view[5], view[9]  };

    m_vertex_data.clear();
    m_index_data.clear();

    uint16_t vert_offset = 0;

    for (const auto& em : m_emitters) {
        for (const auto& p : em.particles) {
            if (p.alpha < 0.01f || p.cur_size <= 0.0f) continue;

            const float half_sz = p.cur_size * 0.5f;
            const float c = std::cos(p.rotation) * half_sz;
            const float s = std::sin(p.rotation) * half_sz;

            // Rotated camera-facing quad offsets
            const float r_rot[3] = { right[0] * c - up[0] * s,
                                     right[1] * c - up[1] * s,
                                     right[2] * c - up[2] * s };
            const float u_rot[3] = { right[0] * s + up[0] * c,
                                     right[1] * s + up[1] * c,
                                     right[2] * s + up[2] * c };

            float pr = p.r;
            float pg = p.g;
            float pb = p.b;

            if (em.is_fire) {
                // Realistic thermal gradient across flame lifetime:
                // - Early life (s < 0.22): white-hot incandescent wick / core
                // - Mid life (0.22 <= s <= 0.55): bright golden-yellow / vivid flame
                // - Late life (s > 0.55): cooling ember tips turning amber and deep crimson
                const float s = std::clamp(p.age / p.max_age, 0.0f, 1.0f);
                if (s < 0.22f) {
                    const float t = s / 0.22f; // 0 at birth -> 1 at 0.22
                    pr = std::min(1.0f, pr * 1.15f);
                    pg = std::min(1.0f, pg * 1.35f);
                    pb = std::min(1.0f, pb + (1.0f - t) * 0.55f); // intense incandescent white core
                } else if (s > 0.55f) {
                    const float t = (s - 0.55f) / 0.45f; // 0 at 0.55 -> 1 at death
                    pr = pr * (1.0f - t * 0.20f);
                    pg = pg * (1.0f - t * 0.65f);
                    pb = pb * (1.0f - t * 0.90f);
                }
            }

            const float col_a = p.base_a * p.alpha;
            const uint8_t cr = static_cast<uint8_t>(std::clamp(pr, 0.0f, 1.0f) * 255.0f);
            const uint8_t cg = static_cast<uint8_t>(std::clamp(pg, 0.0f, 1.0f) * 255.0f);
            const uint8_t cb = static_cast<uint8_t>(std::clamp(pb, 0.0f, 1.0f) * 255.0f);
            const uint8_t ca = static_cast<uint8_t>(std::clamp(col_a, 0.0f, 1.0f) * 255.0f);

            // 4 vertices matching 24-byte interleaved layout:
            // v0: bottom-left
            m_vertex_data.push_back({
                { p.x - r_rot[0] - u_rot[0], p.y - r_rot[1] - u_rot[1], p.z - r_rot[2] - u_rot[2] },
                { 0.0f, 0.0f },
                { cr, cg, cb, ca }
            });
            // v1: bottom-right
            m_vertex_data.push_back({
                { p.x + r_rot[0] - u_rot[0], p.y + r_rot[1] - u_rot[1], p.z + r_rot[2] - u_rot[2] },
                { 1.0f, 0.0f },
                { cr, cg, cb, ca }
            });
            // v2: top-right
            m_vertex_data.push_back({
                { p.x + r_rot[0] + u_rot[0], p.y + r_rot[1] + u_rot[1], p.z + r_rot[2] + u_rot[2] },
                { 1.0f, 1.0f },
                { cr, cg, cb, ca }
            });
            // v3: top-left
            m_vertex_data.push_back({
                { p.x - r_rot[0] + u_rot[0], p.y - r_rot[1] + u_rot[1], p.z - r_rot[2] + u_rot[2] },
                { 0.0f, 1.0f },
                { cr, cg, cb, ca }
            });

            // 2 triangles: (0, 1, 2) and (0, 2, 3)
            m_index_data.push_back(vert_offset + 0);
            m_index_data.push_back(vert_offset + 1);
            m_index_data.push_back(vert_offset + 2);
            m_index_data.push_back(vert_offset + 0);
            m_index_data.push_back(vert_offset + 2);
            m_index_data.push_back(vert_offset + 3);

            vert_offset += 4;
            if (vert_offset >= 65500) break; // uint16 limit guard
        }
    }

    if (m_index_data.empty()) return;

    // Upload geometry to dynamic buffers (24-byte stride)
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vertex_data.size() * sizeof(Vertex)),
                 m_vertex_data.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_index_data.size() * sizeof(uint16_t)),
                 m_index_data.data(), GL_DYNAMIC_DRAW);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Fiery / luminous additive blending
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    glUseProgram(m_prog);
    glUniformMatrix4fv(m_loc_viewproj, 1, GL_FALSE, view_proj);
    glUniform1i(m_loc_has_tex, particle_texture != 0 ? 1 : 0);
    glUniform1i(m_loc_texture, 0);

    if (particle_texture != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, particle_texture);
    }

    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_index_data.size()), GL_UNSIGNED_SHORT, nullptr);

    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);
    if (particle_texture != 0) glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace ruby::render
