#pragma once
// gl_pipeline.h — the preview renderer's GPU layer, in `caver::render`.
//
// WHY THIS EXISTS
// ---------------
// The preview used to draw through `av::render_*`, which is the studio's
// editor renderer (fixed-function matrix stacks, editor grid, gizmo passes,
// picking). The preview is not an editor: it is the game. This is a GL 3.3 CORE
// pipeline of its own — real VAO/VBO/EBO geometry, real GLSL 330 shaders, no
// immediate mode, no matrix stacks.
//
// The lighting model is the one the Ruby GG viewport uses for Swordigo worlds,
// ported here without Qt:
//
//   ruby::render::ShaderLibrary      caver::render::kSceneFS below
//   ─────────────────────────────    ──────────────────────────────
//   halflambert(NdotL)               halflambert()      Half-Lambert² diffuse
//   hemisphere_ambient()             hemisphere_ambient()
//   apply_fog_exp2()                 apply_fog_exp2()
//   tonemap_reinhard()               tonemap_reinhard() L_white 2.5
//   swordigo_grade()                 swordigo_grade()   +15% sat, warm push
//   gamma_out()                      gamma_out()        pow(1/1.6)
//
// and its view-space convention (camera at the origin, light directions rotated
// into view space, `gl_Position = uProj * (uModelView * aPos)`) so the two
// renderers agree on what a Swordigo frame looks like.
//
// What it deliberately does NOT do: editing (gizmos, picking, grid, selection),
// ImGui, or any scene-authoring state. It draws what `caver::` simulates.

#include <string>
#include <vector>

#include "tools/av_renderer.h"   // av::PODMesh + the pure mat4 helpers
#include "tools/pod_loader.h"

namespace caver {
namespace render {

// ── A mesh resident on the GPU ───────────────────────────────────────────────
// Interleaved [pos(3) normal(3) uv(2)] + u32 indices. The same layout the
// viewport uses, so a mesh uploaded here and one uploaded there are identical
// apart from who owns the handles.
struct GlMesh {
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
    int  index_count  = 0;
    int  vertex_count = 0;
    bool flip_uv_v    = false;   // POD uv origin is the image bottom
    bool ready() const { return vao != 0 && index_count > 0; }
};

// ── Lights, straight off the scene's Light/FireEmitter components ────────────
struct PointLight {
    float pos[3]   = {0.0f, 0.0f, 0.0f};
    float color[3] = {1.0f, 1.0f, 1.0f};   // linear, already scaled by intensity
    float radius   = 100.0f;
};

struct DirLight {
    float dir[3]   = {0.0f, 1.0f, 0.0f};   // world-space direction TOWARD the light
    float color[3] = {1.0f, 1.0f, 1.0f};
};

// ── Material (PBR-lite, as the viewport's MaterialParams) ────────────────────
struct Material {
    float        base_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float        emissive[3]   = {0.0f, 0.0f, 0.0f};
    unsigned int texture       = 0;        // 0 = untextured
    // Swordigo's textures are PVRTC/ETC1 decoded to RGBA8 with NO sRGB tagging,
    // and the original GLES1.1 renderer sampled them as-is. The Ruby GG viewport
    // does the same (viewport_shader.cpp: "we do the same — NO pow(2.2) decode —
    // to preserve the original palette"), so the default is off: decoding would
    // darken every level by pow(albedo, 2.2).
    bool         texture_is_srgb = false;
    float        roughness     = 0.75f;    // kept for the material record
    float        metalness     = 0.0f;
    float        uv_offset[2]  = {0.0f, 0.0f};   // scrolling (water, backgrounds)
};

// ── GlPipeline ───────────────────────────────────────────────────────────────
class GlPipeline {
public:
    GlPipeline() = default;
    ~GlPipeline();

    GlPipeline(const GlPipeline&) = delete;
    GlPipeline& operator=(const GlPipeline&) = delete;

    // Compile the shaders. Call with a current GL 3.3 core context.
    bool init(std::string* error = nullptr);
    void shutdown();
    bool ready() const { return scene_program_ != 0; }

    // ── frame ────────────────────────────────────────────────────────────
    // Size the scene colour+depth target. w/h <= 0 renders straight to the
    // default framebuffer.
    bool resize(int w, int h);
    // Bind the scene target, clear it, and set the frame's matrices.
    void begin_scene(const float proj[16], const float view[16]);
    void end_scene();
    // Composite the scene target onto `default_fbo` (0 = the window).
    void composite(unsigned int default_fbo);

    int  target_width() const { return width_; }
    int  target_height() const { return height_; }
    unsigned int scene_texture() const { return color_tex_; }

    // ── lighting ────────────────────────────────────────────────────────
    void set_point_lights(const std::vector<PointLight>& lights);
    void set_dir_lights(const std::vector<DirLight>& lights);
    void set_ambient(const float sky[3], const float ground[3]);
    void set_fog(bool enabled, const float color[3], float density);
    void set_exposure(float exposure) { exposure_ = exposure; }

    // ── geometry ────────────────────────────────────────────────────────
    void upload_mesh(const av::PODMesh& mesh, GlMesh& out, bool flip_uv_v = false);
    // Re-upload only the vertex stream of an already-uploaded mesh (the CPU
    // skinning path writes here every frame).
    void update_vertices(GlMesh& mesh, const float* positions, const float* normals,
                         const float* uvs, int count);
    static void free_mesh(GlMesh& mesh);

    // Draw one mesh with `model` (column-major, world space).
    void draw(const GlMesh& mesh, const float model[16], const Material& material);

    // ── light overlay ───────────────────────────────────────────────────
    // Caver::LightOverlay, recovered from the binary (Scene::Draw 0x3755EC
    // builds one overlay per scene and draws it over the ground/models): a black
    // veil at alpha 0.5 over the visible camera rect, with a bright hole punched
    // around every point light. `alpha` should be 0.5 * the scene's overlay
    // (LightComponent type 4) intensity. Requires begin_scene() to have run.
    void draw_light_veil(const std::vector<PointLight>& lights, float alpha);

    // ── background (BackgroundComponent) ────────────────────────────────
    // Caver::Scene::Draw runs the BackgroundComponent pass LAST, with the
    // camera's 2D projection (Scene::Draw 0x3755EC sets the 2D matrix and then
    // enables the depth test), so a background fills exactly the pixels the world
    // left untouched. `half_extent` is the quad's NDC half size, `offset` its
    // parallax shift, both in NDC; the quad sits just inside the far plane.
    void draw_background(unsigned int texture, const float offset[2],
                         const float half_extent[2], const float color[4]);

    // ── billboards ──────────────────────────────────────────────────────
    // A torch flame / light glow: an additive radial sprite at `pos`.
    void draw_glow(const float pos[3], const float color[3], float size, float intensity);
    // A soft dark ellipse under a character (the engine's shadow blob).
    void draw_shadow(const float pos[3], float radius_x, float radius_z, const float color[3]);
    // Upload and draw every queued sprite. Call once per frame before end_scene.
    void flush_sprites();

    // Diagnostics for the shell's stats line.
    int draw_calls() const { return draw_calls_; }
    int triangles() const { return triangles_; }

private:
    struct SpriteVertex {
        float x, y, z;      // world space (built against the camera basis)
        float u, v;         // quad UV
        float r, g, b, a;   // per-sprite colour + intensity
    };

    void queue_sprite(const float pos[3], const float color[3], float radius_x, float radius_z,
                      float intensity, bool additive);

    bool  build_programs(std::string* error);
    void  scene_uniforms();
    void  upload_sprite_buffer(bool additive);

    // ── programs ────────────────────────────────────────────────────────
    unsigned int scene_program_ = 0;
    unsigned int sprite_program_ = 0;
    unsigned int blit_program_ = 0;
    unsigned int veil_program_ = 0;

    // Cached uniform locations (scene program).
    int u_proj_ = -1, u_model_view_ = -1, u_normal_mat_ = -1;
    int u_dir_count_ = -1, u_dir_dir_ = -1, u_dir_col_ = -1;
    int u_point_count_ = -1, u_point_pos_ = -1, u_point_col_ = -1, u_point_radius_ = -1;
    int u_amb_sky_ = -1, u_amb_ground_ = -1, u_world_up_view_ = -1;
    int u_fog_enabled_ = -1, u_fog_color_ = -1, u_fog_density_ = -1;
    int u_exposure_ = -1, u_uv_offset_ = -1;
    int u_base_color_ = -1, u_emissive_ = -1, u_has_texture_ = -1, u_albedo_tex_ = -1,
        u_tex_is_srgb_ = -1, u_flip_v_ = -1;
    // Cached uniform locations (sprite / blit programs).
    int s_proj_ = -1, s_view_ = -1;
    int b_tex_ = -1, b_flip_ = -1;
    // Cached uniform locations (light-overlay veil program).
    int v_alpha_ = -1, v_count_ = -1, v_pos_ = -1, v_radius_ = -1, v_aspect_ = -1;
    unsigned int veil_vao_ = 0;
    // Cached uniform locations (background program).
    int g_offset_ = -1, g_extent_ = -1, g_color_ = -1, g_tex_ = -1, g_depth_ = -1;
    unsigned int background_program_ = 0;

    // ── target ──────────────────────────────────────────────────────────
    unsigned int fbo_ = 0, color_tex_ = 0, depth_rb_ = 0;
    int width_ = 0, height_ = 0;
    unsigned int quad_vao_ = 0, sprite_vbo_[2] = {0, 0};
    std::vector<SpriteVertex> sprites_[2];   // [0] = glow (additive), [1] = shadow

    // ── frame state ─────────────────────────────────────────────────────
    float proj_[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float view_[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float cam_right_[3] = {1.0f, 0.0f, 0.0f};
    float cam_up_[3]    = {0.0f, 1.0f, 0.0f};
    std::vector<PointLight> point_lights_;
    std::vector<DirLight>   dir_lights_;
    float ambient_sky_[3]    = {0.40f, 0.42f, 0.46f};
    float ambient_ground_[3] = {0.19f, 0.17f, 0.14f};
    bool  fog_enabled_ = false;
    float fog_color_[3] = {0.07f, 0.075f, 0.086f};
    float fog_density_  = 0.00035f;
    float exposure_     = 1.0f;
    bool  in_scene_     = false;
    int   draw_calls_   = 0;
    int   triangles_    = 0;
    std::string error_;
};

} // namespace render
} // namespace caver
