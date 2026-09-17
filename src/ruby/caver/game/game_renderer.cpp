// game_renderer.cpp — the draw path for the recovered runtime.
//
// This file is *mapping*, never *invention*:
//
//   * pods / ground meshes / backgrounds / textures resolve through av::
//     (scene_loader + scene_asset_resolver), the same loaders the studio uses,
//     so an asset that renders here renders in Ruby and back;
//   * transforms come from swk::object_world_matrix / object_render_matrix, so
//     node centring, the ModelComponent's baked Y-rotation and template scaling
//     are handled by the shared code, not re-derived here;
//   * the camera is the engine's own framing (InitWithScene / CameraController),
//     passed in by the caller — nothing about the view is hardcoded;
//   * lights, fires, shadows and fluid sheets come from the components the
//     scene loader already parsed (SceneLight / SceneShadow / SceneWater).

#define GL_GLEXT_PROTOTYPES 1
#include "platform/gl_inc.h"
#include <GL/glext.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_set>

#include "ruby/caver/game/game_renderer.h"
#include "tools/scene_asset_resolver.h"
#include "tools/scene_workspace.h"
#include "platform/pvr_loader.h"

namespace fs = std::filesystem;

namespace caver {
namespace game {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Vertex count of an uploaded mesh (the dump reports level density).
int mesh_vertices(const render::GlMesh& mesh) { return mesh.vertex_count; }

// The scene loader hands every object *some* mesh name: when template
// resolution yields no Model component it falls back to the TemplateName so the
// studio can preview a bare decoration (bush, grove_tree1, …). The engine draws
// nothing for such an object — the archetype it references may have no model at
// all (`point_250_15`, `Template 1`) — so the game render path asks for the
// Model component itself rather than trusting the fallback.
bool object_has_a_model(const av::SceneObject& object) {
    const auto& components = object.resolved_components.empty() ? object.components
                                                               : object.resolved_components;
    for (const auto& component : components) {
        if (component.type_name == "MeshRenderer" || component.type_name == "SkinnedMeshRenderer")
            return true;
        if (component.payload_field == 101 || component.payload_field == 102)
            return true;   // Model / MeshRenderer payload slots
        const std::string name = av::scene_component_class_name(component);
        if (name == "ModelComponent" || name == "MeshRenderer" ||
            name == "SkinnedMeshRenderer" || name == "SpriteComponent")
            return true;
    }
    return false;
}

} // namespace

GameRenderer::GameRenderer() = default;
GameRenderer::~GameRenderer() { shutdown(); }

// ============================================================================
// Lifecycle
// ============================================================================

bool GameRenderer::init(const Options& options) {
    width_  = std::max(64, options.width);
    height_ = std::max(64, options.height);
    visible_ = options.visible;
    (void)visible_;
    postfx_  = options.postfx;
    layers_  = options.layers;

    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        error_ = std::string("SDL_Init: ") + SDL_GetError();
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!visible_) flags |= SDL_WINDOW_HIDDEN;
    window_ = SDL_CreateWindow(options.title.c_str(), width_, height_, flags);
    if (!window_) {
        error_ = std::string("SDL_CreateWindow: ") + SDL_GetError();
        return false;
    }

    gl_context_ = SDL_GL_CreateContext(window_);
    if (!gl_context_) {
        error_ = std::string("SDL_GL_CreateContext: ") + SDL_GetError();
        return false;
    }
    SDL_GL_MakeCurrent(window_, static_cast<SDL_GLContext>(gl_context_));
    sword_init_gl_after();
    SDL_GL_SetSwapInterval(options.vsync ? 1 : 0);

    // The draw path: caver's own GL 3.3 core pipeline with real GLSL 330
    // shaders (the Ruby GG viewport shader, ported off Qt). av::'s editor draw
    // path is not used by the preview at all.
    std::string shader_error;
    if (!pipeline_.init(&shader_error)) {
        error_ = "gl_pipeline: " + shader_error;
        return false;
    }
    build_quad_mesh();

    // The drawable's real pixel size (2x the requested size on a HiDPI screen).
    {
        int pixel_w = 0, pixel_h = 0;
        SDL_GetWindowSizeInPixels(window_, &pixel_w, &pixel_h);
        if (pixel_w > 0 && pixel_h > 0) {
            width_ = pixel_w;
            height_ = pixel_h;
        }
    }

    if (!pipeline_.resize(width_, height_))
        warnings_.push_back("scene framebuffer unavailable — drawing straight to the window");

    std::printf("opensw: renderer up — %dx%d GL 3.3 core, glsl330 shaders, "
                "target=%s\n",
                width_, height_, pipeline_.scene_texture() ? "fbo" : "window");
    return true;
}

void GameRenderer::shutdown() {
    for (auto& [key, model] : models_) {
        for (auto& mesh : model.gpu) render::GlPipeline::free_mesh(mesh);
    }
    models_.clear();
    for (auto& object : objects_) {
        for (auto& mesh : object.ground) render::GlPipeline::free_mesh(mesh);
    }
    objects_.clear();
    render::GlPipeline::free_mesh(quad_mesh_);
    for (auto& [path, tex] : textures_) {
        if (tex) glDeleteTextures(1, &tex);
    }
    textures_.clear();
    pipeline_.shutdown();
    if (gl_context_) { SDL_GL_DestroyContext(static_cast<SDL_GLContext>(gl_context_)); gl_context_ = nullptr; }
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
    SDL_Quit();
}

// ============================================================================
// Resources — pods, textures, ground meshes
// ============================================================================

unsigned int GameRenderer::upload_rgba(const uint8_t* pixels, int w, int h) {
    if (!pixels || w <= 0 || h <= 0) return 0;
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    return tex;
}

unsigned int GameRenderer::load_texture_file(const std::string& path) {
    if (path.empty() || !fs::exists(path)) return 0;
    auto cached = textures_.find(path);
    if (cached != textures_.end()) return cached->second;

    // The shipped textures come in three containers, and only the first two are
    // real images to SDL_image:
    //   *.pvr            legacy PVR header + ETC1/RGBA8888 (PVRTTextureLoadFromPointer)
    //   *.tex.png        the game's NATIVE tex container {type,w,h}+pixels, gzipped
    //                    (this is what every scene Background is authored with)
    //   *.png / others   plain images (mods, imported textures)
    // pvr_load_texture handles the gzip + native-tex + PVR cases, so the content
    // magic decides, not the extension: a `.tex.png` is 1f 8b, not a PNG.
    uint8_t magic[8] = {0};
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file) {
        const size_t got = std::fread(magic, 1, sizeof(magic), file);
        (void)got;
        std::fclose(file);
    }
    const bool gzip = magic[0] == 0x1f && magic[1] == 0x8b;
    // PVR v3 carries the "PVR!" magic at offset 0, but the legacy 44-byte v2
    // header that most of the shipped sheets use puts it at offset 44 — so the
    // extension is the reliable signal, with the magic as a bonus.
    const bool pvr_magic = magic[0] == 'P' && magic[1] == 'V' && magic[2] == 'R';
    std::string lower_path = path;
    for (char& c : lower_path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const bool pvr_ext = lower_path.size() > 4 &&
                         lower_path.compare(lower_path.size() - 4, 4, ".pvr") == 0;

    unsigned int tex = 0;
    int tex_w = 0, tex_h = 0;
    if (gzip || pvr_magic || pvr_ext) {
        // PVR / gzipped native tex: the game's own decoder, uploaded as-is.
        tex = pvr_load_texture(path.c_str(), &tex_w, &tex_h);
    } else {
        SDL_Surface* surface = IMG_Load(path.c_str());
        if (surface) {
            SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ABGR8888);
            if (rgba) {
                tex_w = rgba->w;
                tex_h = rgba->h;
                tex = upload_rgba(static_cast<const uint8_t*>(rgba->pixels), rgba->w, rgba->h);
                SDL_DestroySurface(rgba);
            }
            SDL_DestroySurface(surface);
        }
    }
    textures_[path] = tex;
    if (tex && tex_w > 0 && tex_h > 0)
        texture_aspects_[tex] = static_cast<float>(tex_w) / static_cast<float>(tex_h);
    return tex;
}

unsigned int GameRenderer::texture_for(const std::string& texture_name,
                                       const std::string& owner_path) {
    if (texture_name.empty()) return 0;
    const fs::path owner(owner_path.empty() ? scene_path_ : owner_path);
    for (const auto& candidate : av::assets::texture_candidates(owner, texture_name,
                                                                std::vector<fs::path>{})) {
        if (!fs::exists(candidate)) continue;
        const unsigned int tex = load_texture_file(candidate.string());
        if (tex) return tex;
    }
    return 0;
}

const RenderModel* GameRenderer::model_from_path(const std::string& pod_path,
                                                const std::string& merge_hint) {
    if (pod_path.empty()) return nullptr;
    auto cached = models_.find(pod_path);
    if (cached != models_.end()) return &cached->second;

    RenderModel model;
    model.path = pod_path;
    model.pod = av::pod_load(pod_path, merge_hint);
    if (model.pod.meshes.empty()) {
        // Remember the miss so a scene full of a missing pod does not re-parse it.
        models_[pod_path] = std::move(model);
        warnings_.push_back("pod has no meshes: " + pod_path);
        return &models_[pod_path];
    }

    model.gpu.reserve(model.pod.meshes.size());
    for (const auto& mesh : model.pod.meshes) {
        render::GlMesh gpu;
        // Swordigo POD UVs put v = 0 at the image bottom, which is the GL
        // convention, so no flip is needed for shipped assets.
        pipeline_.upload_mesh(mesh, gpu, false);
        model.gpu.push_back(gpu);
    }
    model.textures.resize(model.pod.texture_filenames.size(), 0);
    int resolved_textures = 0;
    for (size_t i = 0; i < model.pod.texture_filenames.size(); ++i) {
        model.textures[i] = texture_for(model.pod.texture_filenames[i], pod_path);
        if (model.textures[i]) ++resolved_textures;
    }
    if (!model.pod.texture_filenames.empty() && resolved_textures == 0) {
        // A pod whose every sheet is missing draws flat-lit (base colour only),
        // which is exactly what a wrong asset root or a modded pod looks like.
        warnings_.push_back("pod textures not found: " + pod_path +
                            " (" + model.pod.texture_filenames[0] + ")");
    }
    if (!model.textures.empty() && model.textures[0] == 0) {
        // Fall back to the pod's own stem (many Swordigo models name the
        // texture after the model rather than in the material table).
        const fs::path pod(pod_path);
        model.textures[0] = texture_for(pod.stem().string(), pod_path);
    }
    model.feet_offset = av::pod_feet_offset(model.pod);

    auto inserted = models_.emplace(pod_path, std::move(model));
    return &inserted.first->second;
}

const RenderModel* GameRenderer::model_for(const std::string& mesh_name) {
    if (mesh_name.empty()) return nullptr;
    const fs::path resolved = av::assets::resolve_pod(scene_path_, mesh_name,
                                                      std::vector<fs::path>{});
    if (resolved.empty()) {
        warnings_.push_back("pod not found for mesh '" + mesh_name + "'");
        return nullptr;
    }
    return model_from_path(resolved.string(), mesh_name);
}

bool GameRenderer::build_level(const av::SceneData& scene) {
    if (!window_) { error_ = "build_level before init()"; return false; }
    scene_path_ = scene.filepath;
    if (scene_path_.empty() && !scene.filename.empty())
        scene_path_ = (fs::path("resources") / scene.filename).string();
    scene_dir_ = fs::path(scene_path_).parent_path().string();

    objects_.clear();
    by_scene_index_.clear();
    waters_ = scene.waters;
    lights_ = scene.lights;
    shadows_ = scene.shadows;
    overlays_ = scene.overlays;

    // SceneObjectGroup.Hidden (tag 3): the group's members start hidden. Caver::
    // Scene::SetGroupHidden flips exactly this at runtime, and scenes use it for
    // their day/night sets (`Scene.SetGroupHidden("night", ...)`).
    std::unordered_set<std::string> hidden_by_group;
    for (const auto& group : scene.parsed_groups) {
        if (!group.hidden) continue;
        for (const auto& member : group.members) hidden_by_group.insert(member);
    }
    // The day/night pair is authored as two objects that differ only in their
    // identifier suffix and their texture. A fresh profile (0 progress) is day —
    // the shipped `newplayer.gstate` — so with nothing else to go on the _night
    // half of a _day/_night pair is not drawn. Any scene that switches it does so
    // through SetGroupHidden once its scripts run.
    std::unordered_set<std::string> night_members;
    for (const auto& group : scene.parsed_groups) {
        if (group.name == "night") {
            for (const auto& member : group.members) night_members.insert(member);
        }
    }

    const size_t total = scene.objects.size();
    size_t template_only = 0;   // no Model component: the loader's name fallback only
    size_t hidden_objects = 0;
    for (size_t i = 0; i < total; ++i) {
        const av::SceneObject& object = scene.objects[i];
        if (object.hidden) { ++hidden_objects; continue; }
        if (hidden_by_group.count(object.name)) { ++hidden_objects; continue; }
        // A _night member of a _day/_night pair (Background_night /
        // DirectionalLight_night) is the `night` group's own object.
        if (night_members.count(object.name)) {
            const std::string day = object.name.substr(0, object.name.size() - 6) + "_day";
            bool has_day = false;
            for (const auto& other : scene.objects)
                if (other.name == day) { has_day = true; break; }
            if (has_day) { ++hidden_objects; continue; }
        }

        RenderObject render;
        render.scene_index = static_cast<int>(i);
        render.identifier = object.name;
        render.live = object;
        render.is_portal = object.is_portal;
        render.is_dimension = object.is_dimension_object;

        if (!object.mesh_name.empty() && object_has_a_model(object))
            render.model = model_for(object.mesh_name);
        else if (!object.mesh_name.empty())
            ++template_only;

        for (size_t m = 0; m < object.ground_meshes.size(); ++m) {
            const av::PODMesh& mesh = object.ground_meshes[m];
            if (mesh.num_vertices <= 0) continue;
            render::GlMesh gpu;
            pipeline_.upload_mesh(mesh, gpu, false);
            render.ground.push_back(gpu);
            render.ground_textures.push_back(
                m < object.ground_mesh_textures.size()
                    ? texture_for(object.ground_mesh_textures[m], scene_path_)
                    : 0u);
        }

        if (!object.background_name.empty()) {
            render.background_tex = texture_for(object.background_name, scene_path_);
            if (!render.background_tex)
                warnings_.push_back("background texture not found for '" + object.name +
                                    "' (" + object.background_name + ")");
        }

        const bool drawable = render.model || !render.ground.empty() || render.background_tex;
        if (!drawable) continue;
        by_scene_index_[render.scene_index] = objects_.size();
        objects_.push_back(std::move(render));
    }

    std::printf("opensw: renderer built %zu/%zu objects, %zu distinct pods, %zu object-textures\n",
                objects_.size(), total, models_.size(), textures_.size());
    if (hidden_objects > 0)
        std::printf("opensw: %zu objects hidden (group hidden / night half of a day-night pair)\n",
                    hidden_objects);
    if (template_only > 0)
        std::printf("opensw: %zu objects reference an archetype with no Model component "
                    "(nothing to draw)\n", template_only);
    return true;
}

// ============================================================================
// Actors
// ============================================================================

int GameRenderer::add_actor(const std::string& key, const av::SceneObject& archetype) {
    RenderActor actor;
    actor.key = key;
    actor.template_name = archetype.template_name;
    if (!archetype.mesh_name.empty()) actor.model = model_for(archetype.mesh_name);
    actor.has_model_y_rotation = archetype.has_model_y_rotation;
    actor.model_y_rotation = archetype.model_y_rotation;
    actor.scale = archetype.scale_x != 0.0f ? archetype.scale_x : 1.0f;
    if (actor.model) actor.feet_offset = actor.model->feet_offset;
    actors_.push_back(actor);
    return static_cast<int>(actors_.size() - 1);
}

void GameRenderer::clear_actors() { actors_.clear(); }

int GameRenderer::find_actor(const std::string& key) const {
    for (size_t i = 0; i < actors_.size(); ++i)
        if (actors_[i].key == key) return static_cast<int>(i);
    return -1;
}

RenderActor* GameRenderer::actor(int index) {
    if (index < 0 || index >= static_cast<int>(actors_.size())) return nullptr;
    return &actors_[static_cast<size_t>(index)];
}

const RenderActor* GameRenderer::actor(int index) const {
    if (index < 0 || index >= static_cast<int>(actors_.size())) return nullptr;
    return &actors_[static_cast<size_t>(index)];
}

void GameRenderer::set_actor(int index, const float pos[3], float rot, float facing,
                             float scale, float anim_time, bool hidden) {
    RenderActor* a = actor(index);
    if (!a) return;
    a->pos[0] = pos[0];
    a->pos[1] = pos[1];
    a->pos[2] = pos[2];
    a->rot = rot;
    a->facing = facing < 0.0f ? -1.0f : 1.0f;
    if (scale != 0.0f) a->scale = scale;
    a->anim_time = anim_time;
    a->hidden = hidden;
}

void GameRenderer::set_actor_anim(int index, float anim_time) {
    if (RenderActor* a = actor(index)) a->anim_time = anim_time;
}

void GameRenderer::set_actor_clip(int index, const std::string& clip_name, float clip_seconds) {
    RenderActor* a = actor(index);
    if (!a) return;
    a->clip_time = clip_seconds;
    if (clip_name == a->clip) return;
    a->clip = clip_name;
    a->clip_model = nullptr;
    if (clip_name.empty()) return;
    // An animation pod is named by the KeyframeAnimation and merged onto the base
    // model by the loader (hiro_stand.POD -> hiro.POD), so resolving it here is
    // all it takes to swap clips. A miss falls back to the archetype's model.
    a->clip_model = model_for(clip_name);
}

void GameRenderer::dump_objects() const {
    std::printf("opensw: %d objects, %zu pods, %zu textures\n",
                static_cast<int>(objects_.size()), models_.size(), textures_.size());
    for (const auto& object : objects_) {
        const int ground_verts = [&] {
            int n = 0;
            for (const auto& g : object.ground) n += static_cast<int>(mesh_vertices(g));
            return n;
        }();
        std::printf("  [%4d] %-28s pos=(%9.2f,%9.2f,%9.2f) rot=(%.2f,%.2f,%.2f) "
                    "scale=(%.3f,%.3f,%.3f) model=%s ground=%zu(%d verts) bg=%u%s\n",
                    object.scene_index, object.identifier.c_str(),
                    object.live.pos_x, object.live.pos_y, object.live.pos_z,
                    object.live.rot_x, object.live.rot_y, object.live.rot_z,
                    object.live.scale_x, object.live.scale_y, object.live.scale_z,
                    object.model ? object.model->path.c_str() : "-",
                    object.ground.size(), ground_verts, object.background_tex,
                    object.live.hidden ? " HIDDEN" : "");
    }
}

void GameRenderer::sync_object(int scene_index, const float pos[3], float rot, float scale,
                               float anim_time, bool hidden) {
    auto it = by_scene_index_.find(scene_index);
    if (it == by_scene_index_.end()) return;
    RenderObject& render = objects_[it->second];
    av::SceneObject& live = render.live;
    live.pos_x = pos[0];
    live.pos_y = pos[1];
    live.pos_z = pos[2];
    live.rot_y = rot;
    live.scale_x = scale;
    live.scale_y = scale;
    live.scale_z = scale;
    live.hidden = hidden;
    render.anim_time = anim_time;
}

// ============================================================================
// Window / input
// ============================================================================

bool GameRenderer::pump_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                quit_ = true;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                int pixel_w = 0, pixel_h = 0;
                SDL_GetWindowSizeInPixels(window_, &pixel_w, &pixel_h);
                if (pixel_w <= 0 || pixel_h <= 0) {
                    pixel_w = event.window.data1;
                    pixel_h = event.window.data2;
                }
                width_  = std::max(64, pixel_w);
                height_ = std::max(64, pixel_h);
                resized_ = true;
                break;
            }
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                const int scancode = static_cast<int>(event.key.scancode);
                if (scancode > 0) {
                    if (keys_.size() <= static_cast<size_t>(scancode)) keys_.resize(scancode + 1, 0);
                    keys_[static_cast<size_t>(scancode)] = event.key.down ? 1 : 0;
                }
                break;
            }
            default:
                break;
        }
    }
    return !quit_;
}

bool GameRenderer::key_down(int scancode) const {
    if (scancode < 0 || static_cast<size_t>(scancode) >= keys_.size()) return false;
    return keys_[static_cast<size_t>(scancode)] != 0;
}

bool GameRenderer::resized() {
    const bool was = resized_;
    resized_ = false;
    return was;
}

void GameRenderer::set_title(const std::string& title) {
    if (window_) SDL_SetWindowTitle(window_, title.c_str());
}

// ============================================================================
// Draw
// ============================================================================

void GameRenderer::draw_model(const RenderModel& model, const float model_matrix[16],
                              float anim_seconds) {
    const auto& pod = model.pod;
    // Seconds -> frame number using the pod's own frame rate (Swordigo PODs
    // carry it: PODModel::fps). Looped clips wrap like the engine's player.
    float frame = anim_seconds * (pod.fps > 0.0f ? pod.fps : 30.0f);
    if (pod.num_frames > 0) {
        const float span = static_cast<float>(pod.num_frames);
        frame = std::fmod(frame, span);
        if (frame < 0.0f) frame += span;
    } else {
        frame = 0.0f;
    }
    if (pod.nodes.empty()) {
        for (size_t i = 0; i < model.gpu.size(); ++i) {
            const render::GlMesh& gpu = model.gpu[i];
            if (!gpu.ready()) continue;
            render::Material material;
            if (i < pod.materials.size()) {
                material.base_color[0] = pod.materials[i].diffuse[0];
                material.base_color[1] = pod.materials[i].diffuse[1];
                material.base_color[2] = pod.materials[i].diffuse[2];
                material.base_color[3] = pod.materials[i].opacity;
            }
            material.texture = model.textures.empty() ? 0u : model.textures[0];
            pipeline_.draw(gpu, model_matrix, material);
        }
        return;
    }

    for (size_t ni = 0; ni < pod.nodes.size(); ++ni) {
        const av::PODNode& node = pod.nodes[ni];
        if (node.object_index < 0 || node.object_index >= static_cast<int>(model.gpu.size())) continue;
        const render::GlMesh& gpu = model.gpu[static_cast<size_t>(node.object_index)];
        if (!gpu.ready()) continue;

        // Material colour + texture (the scene's per-node material table).
        float color[4] = {1, 1, 1, 1};
        int texture_index = -1;
        if (node.material_index >= 0 && node.material_index < static_cast<int>(pod.materials.size())) {
            const av::PODMaterial& material = pod.materials[static_cast<size_t>(node.material_index)];
            color[0] = material.diffuse[0];
            color[1] = material.diffuse[1];
            color[2] = material.diffuse[2];
            color[3] = material.opacity;
            texture_index = material.diffuse_texture_index;
        }
        render::Material material;
        material.base_color[0] = color[0];
        material.base_color[1] = color[1];
        material.base_color[2] = color[2];
        material.base_color[3] = color[3];
        if (texture_index >= 0 && texture_index < static_cast<int>(model.textures.size()))
            material.texture = model.textures[static_cast<size_t>(texture_index)];
        else if (!model.textures.empty())
            material.texture = model.textures[0];

        float node_matrix[16], centred[16], final_matrix[16];
        av::get_node_matrix(pod, static_cast<int>(ni), frame, node_matrix);
        if (pod.has_center_point) {
            float offset[16];
            av::mat4_translate(offset, -pod.center_point[0], -pod.center_point[1],
                               -pod.center_point[2]);
            av::mat4_multiply(centred, offset, node_matrix);
        } else {
            std::memcpy(centred, node_matrix, sizeof(centred));
        }
        av::mat4_multiply(final_matrix, model_matrix, centred);

        // Skinned meshes: re-skin on the CPU at the current frame (the engine's
        // own bind-pose-relative path).
        const av::PODMesh& source = pod.meshes[static_cast<size_t>(node.object_index)];
        if (source.bones_per_vertex > 0) {
            std::vector<float> skinned_positions, skinned_normals;
            if (av::skin_mesh(pod, static_cast<int>(ni), frame, skinned_positions,
                              skinned_normals)) {
                pipeline_.update_vertices(const_cast<render::GlMesh&>(gpu),
                                          skinned_positions.data(),
                                          skinned_normals.empty() ? nullptr : skinned_normals.data(),
                                          source.uvs.empty() ? nullptr : source.uvs.data(),
                                          source.num_vertices);
            }
        }

        pipeline_.draw(gpu, final_matrix, material);
    }
}

void GameRenderer::draw_object(const RenderObject& object, float /*anim_seconds*/) {
    if (object.live.hidden) return;
    ++stats_.objects;

    if (object.background_tex) return;   // drawn separately (camera-facing)

    if (object.model) {
        float matrix[16];
        swk::object_render_matrix(object.live, matrix);
        draw_model(*object.model, matrix, object.anim_time);
    }

    if (!object.ground.empty() && (layers_ & kLayerGround)) {
        float matrix[16];
        // Ground meshes are authored in the object's own space and are not
        // affected by the ModelComponent's baked Y-rotation.
        swk::object_world_matrix(object.live, matrix);
        for (size_t i = 0; i < object.ground.size(); ++i) {
            const render::GlMesh& gpu = object.ground[i];
            if (!gpu.ready()) continue;
            render::Material material;
            material.texture = i < object.ground_textures.size() ? object.ground_textures[i] : 0u;
            pipeline_.draw(gpu, matrix, material);
        }
    }
}

void GameRenderer::draw_actor(const RenderActor& actor_) {
    // The clip's pod when one is playing (it carries the animation frames), else
    // the archetype's own model.
    const RenderModel* model = actor_.clip_model ? actor_.clip_model : actor_.model;
    if (actor_.hidden || !model || model->gpu.empty()) return;
    ++stats_.actors;

    // Actor placement: engine pos -> translate, in-plane rotation about Z, and
    // the ModelComponent's baked Y-rotation where the archetype carries one.
    float translate[16], rotate[16], scale_m[16], temp[16], matrix[16];
    float pos[3] = {actor_.pos[0], actor_.pos[1], actor_.pos[2]};
    // A Swordigo character model is authored foot-anchored (its rest pose starts
    // at y ~ 0), while the runtime position is the object's ORIGIN — the point the
    // CollisionShape is expressed against. `feet_offset` is therefore where the
    // feet sit in object space (the shape's local min-Y; hiro: -34), supplied by
    // whoever built the actor. add_actor defaults it to the pod's own measured
    // feet so a shape-less archetype still lands correctly.
    pos[1] += actor_.feet_offset * actor_.scale;
    av::mat4_translate(translate, pos[0], pos[1], pos[2]);
    av::mat4_rotate_z(rotate, actor_.rot * 180.0f / kPi);
    av::mat4_identity(scale_m);
    scale_m[0]  = actor_.scale * actor_.facing;
    scale_m[5]  = actor_.scale;
    scale_m[10] = actor_.scale;
    av::mat4_multiply(temp, translate, rotate);
    av::mat4_multiply(matrix, temp, scale_m);

    if (actor_.has_model_y_rotation) {
        float model_rot[16], composed[16];
        av::mat4_rotate_y(model_rot, actor_.model_y_rotation * 180.0f / kPi);
        av::mat4_multiply(composed, matrix, model_rot);
        std::memcpy(matrix, composed, sizeof(matrix));
    }

    draw_model(*model, matrix, actor_.clip_model ? actor_.clip_time : actor_.anim_time);

    if (actor_.hitbox_width > 0.0f) {
        const float shadow_pos[3] = {actor_.pos[0], actor_.pos[1] + 0.5f, actor_.pos[2]};
        const float colour[3] = {0.0f, 0.0f, 0.0f};
        const float radius = std::max(actor_.hitbox_width, 8.0f) * 1.3f;
        pipeline_.draw_shadow(shadow_pos, radius * 2.0f, radius * 2.0f * 0.62f, colour);
    }
}

void GameRenderer::draw_waters(float time_sec) {
    // A water sheet is the object's own quad, textured with the sheet texture
    // and scrolled by its authored tiling offset. The surface colour tints it,
    // which is how the component describes deep vs shallow water.
    if (!quad_mesh_.ready()) return;
    for (const auto& water : waters_) {
        if (water.object_index < 0) continue;
        float matrix[16];
        auto it = by_scene_index_.find(water.object_index);
        if (it != by_scene_index_.end())
            swk::object_world_matrix(objects_[it->second].live, matrix);
        else
            av::mat4_identity(matrix);
        // The sheet occupies its authored rect inside the object's own space.
        float local[16], scale_m[16], sheet[16];
        av::mat4_translate(local, water.rect[0] + water.rect[2] * 0.5f,
                           water.rect[1] + water.rect[3] * 0.5f, 0.0f);
        av::mat4_identity(scale_m);
        scale_m[0] = water.rect[2] * 0.5f;
        scale_m[5] = water.rect[3] * 0.5f;
        av::mat4_multiply(sheet, local, scale_m);
        av::mat4_multiply(matrix, matrix, sheet);

        render::Material material;
        material.texture = texture_for(water.texture, scene_path_);
        material.base_color[0] = water.surface_color[0];
        material.base_color[1] = water.surface_color[1];
        material.base_color[2] = water.surface_color[2];
        material.base_color[3] = 0.78f;
        const float tile = water.tile_size > 0.0f ? water.tile_size : 1.0f;
        material.uv_offset[0] = water.tex_offset[0] + time_sec * 0.06f;
        material.uv_offset[1] = water.tex_offset[1] + std::sin(time_sec * 0.7f) * 0.03f;
        material.uv_offset[0] *= tile;
        material.uv_offset[1] *= tile;
        pipeline_.draw(quad_mesh_, matrix, material);
    }
}

void GameRenderer::draw_shadows() {
    for (const auto& shadow : shadows_) {
        const float colour[3] = {0.0f, 0.0f, 0.0f};
        pipeline_.draw_shadow(shadow.pos, shadow.width_radius * 2.0f,
                              shadow.depth_radius * 2.0f, colour);
    }
}

void GameRenderer::gather_lights(float time_sec) {
    constexpr int kMaxPointLights = 16;
    struct Nearest {
        float distance;
        const av::SceneData::SceneLight* light;
        float intensity;
    };
    std::vector<Nearest> points;
    points.reserve(lights_.size());

    const float* focus = focus_;

    for (const auto& light : lights_) {
        if (light.type == 4) continue;   // overlay lights are a darkness veil
        if (light.type == 2 || light.type == 1) continue;   // handled below
        // Flicker comes from the light's own recovered parameters.
        float intensity = light.base_intensity > 0.0f ? light.base_intensity : light.intensity;
        if (light.flicker) {
            const float phase = time_sec * light.flicker_speed + static_cast<float>(light.object_index) * 0.7f;
            intensity *= 1.0f + light.flicker_amount * std::sin(phase) * 0.5f
                         + light.flicker_amount * std::sin(phase * 2.37f) * 0.25f;
        }
        const float dx = light.pos[0] - focus[0];
        const float dy = light.pos[1] - focus[1];
        points.push_back({dx * dx + dy * dy, &light, intensity});
    }

    std::sort(points.begin(), points.end(),
              [](const Nearest& a, const Nearest& b) { return a.distance < b.distance; });
    if (points.size() > kMaxPointLights) points.resize(kMaxPointLights);

    std::vector<std::array<float, 3>> positions(points.size());
    std::vector<std::array<float, 3>> colors(points.size());
    std::vector<float> radii(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        const auto& light = *points[i].light;
        positions[i] = {light.pos[0], light.pos[1], light.pos[2]};
        colors[i] = {light.color[0] * points[i].intensity,
                     light.color[1] * points[i].intensity,
                     light.color[2] * points[i].intensity};
        radii[i] = light.radius;
    }
    std::vector<render::PointLight> gl_points;
    gl_points.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        render::PointLight gl_light;
        std::memcpy(gl_light.pos, positions[i].data(), sizeof(gl_light.pos));
        std::memcpy(gl_light.color, colors[i].data(), sizeof(gl_light.color));
        gl_light.radius = radii[i];
        gl_points.push_back(gl_light);
    }
    point_lights_ = gl_points;
    pipeline_.set_point_lights(point_lights_);
    gl_points.clear();

    // Directional lights: Caver::Scene::Draw (0x3755EC) feeds each LightComponent
    // straight to glLightfv, so the shader light is intensity * colour (the
    // intensities are authored well below 1: the town's day sun is 0.3).
    std::vector<render::DirLight> gl_dirs;
    for (const auto& light : lights_) {
        if (light.type != 2) continue;
        render::DirLight dir;
        dir.dir[0] = light.pos[0];
        dir.dir[1] = light.pos[1];
        dir.dir[2] = light.pos[2];
        dir.color[0] = light.color[0] * light.intensity;
        dir.color[1] = light.color[1] * light.intensity;
        dir.color[2] = light.color[2] * light.intensity;
        gl_dirs.push_back(dir);
        if (gl_dirs.size() >= 4) break;
    }
    pipeline_.set_dir_lights(gl_dirs);

    // LightComponent type 1 is the scene's ambient (glLightModelfv(GL_LIGHT_MODEL_
    // AMBIENT) in Scene::Draw) and type 4 is the LightOverlay veil, drawn at the
    // grid's own half alpha over the world. Both are authored per scene; when a
    // scene carries no ambient at all the viewport's calibrated default holds.
    float ambient[3] = {0.0f, 0.0f, 0.0f};
    bool has_ambient = false;
    float overlay = 0.0f;
    for (const auto& light : lights_) {
        if (light.type == 1) {
            ambient[0] += light.color[0] * light.intensity;
            ambient[1] += light.color[1] * light.intensity;
            ambient[2] += light.color[2] * light.intensity;
            has_ambient = true;
        } else if (light.type == 4) {
            overlay += light.intensity;
        }
    }
    if (has_ambient) {
        // A flat ambient (GL_LIGHT_MODEL_AMBIENT has no direction), with a slight
        // sky/ground split so surfaces still read as volumes.
        const float sky[3]    = {ambient[0], ambient[1], ambient[2]};
        const float ground[3] = {ambient[0] * 0.65f, ambient[1] * 0.65f, ambient[2] * 0.65f};
        pipeline_.set_ambient(sky, ground);
    }
    veil_alpha_ = std::clamp(overlay * 0.5f, 0.0f, 0.95f);
}

void GameRenderer::queue_light_glows(float time_sec) {
    // Fire sprites for the fire-linked point lights (FireEmitterComponent points
    // at a LightComponent; the loader flags those). Size derives from the light's
    // own illumination radius so the flame and its light always agree.
    for (const auto& light : lights_) {
        if (light.type == 4 || light.type == 2 || light.type == 1) continue;
        if (!light.flicker) continue;
        float intensity = light.base_intensity > 0.0f ? light.base_intensity : light.intensity;
        if (light.flicker) {
            const float phase = time_sec * light.flicker_speed + static_cast<float>(light.object_index) * 0.7f;
            intensity *= 1.0f + light.flicker_amount * std::sin(phase) * 0.5f
                         + light.flicker_amount * std::sin(phase * 2.37f) * 0.25f;
        }
        const float size = std::max(6.0f, light.radius * 0.08f);
        const float flicker = std::clamp(intensity / std::max(0.001f, light.base_intensity), 0.2f, 2.0f);
        pipeline_.draw_glow(light.pos, light.color, size * 3.0f, flicker);
    }
}

void GameRenderer::render(const av::Camera& camera, float time_sec) {
    stats_ = Stats{};
    flicker_seed_ = time_sec;
    focus_[0] = camera.target[0];
    focus_[1] = camera.target[1];
    focus_[2] = camera.target[2];

    // Keep the scene target at the drawable's size: a resize (or the HiDPI pixel
    // size discovered at init) has to resize the FBO too, or the viewport would
    // be larger than the target and clip the scene into a corner.
    if ((pipeline_.target_width() != width_ || pipeline_.target_height() != height_) &&
        pipeline_.scene_texture() != 0) {
        pipeline_.resize(width_, height_);
    }
    resized_ = false;

    // The engine's own camera -> the view-space pipeline (same math the studio's
    // viewport uses: av::camera_get_view_matrix / camera_get_projection).
    float proj[16], view[16];
    const float aspect = static_cast<float>(width_) / static_cast<float>(std::max(1, height_));
    av::camera_get_view_matrix(camera, view);
    av::camera_get_projection(camera, aspect, proj);

    // A Swordigo-leaning sky/ground ambient and a light exp2 fog, both from the
    // recovered device/scene defaults: no authored values, just the viewport's
    // own calibrations so the two renderers agree. A scene that authors its own
    // LightComponents (type 1 ambient / type 2 directional / type 3 point /
    // type 4 overlay) overrides every one of these in gather_lights().
    const float default_sky[3]    = {0.42f, 0.44f, 0.50f};
    const float default_ground[3] = {0.18f, 0.16f, 0.13f};
    pipeline_.set_ambient(default_sky, default_ground);
    const float fog_color[3] = {clear_color_[0], clear_color_[1], clear_color_[2]};
    pipeline_.set_fog(true, fog_color, 0.00016f);
    pipeline_.set_exposure(exposure_);

    // Lights first: the frame's point set drives both the lit surfaces and the
    // LightOverlay veil drawn over them.
    gather_lights(time_sec);
    pipeline_.begin_scene(proj, view);

    if (layers_ & kLayerObjects)
        for (const auto& object : objects_) draw_object(object, object.anim_time);

    // Caver::LightOverlay: Scene::Draw draws the veil immediately after the
    // ground/model passes, so the world is dimmed but the player, particles and
    // sprites drawn afterwards stay at full brightness.
    if ((layers_ & kLayerObjects) && veil_alpha_ > 0.002f)
        pipeline_.draw_light_veil(point_lights_, veil_alpha_);

    if (layers_ & kLayerActors)
        for (const auto& actor_ : actors_) draw_actor(actor_);
    if (layers_ & kLayerShadows) draw_shadows();
    if (layers_ & kLayerLights) queue_light_glows(time_sec);
    if (layers_ & kLayerWater) draw_waters(time_sec);

    // Backgrounds last: Caver::Scene::Draw runs the BackgroundComponent pass at
    // the end of the frame, with the camera's 2D projection and the depth test on,
    // so each background fills exactly the pixels the world left cleared.
    if (layers_ & kLayerBackgrounds) draw_backgrounds(camera);

    pipeline_.end_scene();
    stats_.draw_calls = pipeline_.draw_calls();
    stats_.triangles  = pipeline_.triangles();

    // Composite the scene target onto the window (a plain textured blit: the
    // tone map and the Swordigo grade already ran in the scene shader).
    pipeline_.composite(0);
}

// Caver::BackgroundComponent::Draw (0x292288), as a screen-space pass.
//
// The recovered function is explicit about the shape of a background:
//   * it places the sprite on a fixed world plane, Z = -11000,
//     (`v7 = camera.z + 11000; scale = -v7 / plane_depth`), which is why a
//     background never moves with the camera the way a world object does;
//   * its scale is `0.5 / (plane_distance * tan(fov/2)) * 20 * (1024 / tex_width)`
//     — i.e. the sprite is sized to cover the view, normalised by the texture's
//     own width so a 512 and a 1024 sheet cover the same screen area;
//   * the parallax shift is `-(forward_point - plane_point) * (0.5/(dist*tan))`
//     clamped to +-(half_sprite - 1), so it can never expose an edge.
//
// This keeps every one of those behaviours in NDC, and draws with the depth test
// on: the quad sits just inside the far plane, so it lands wherever the world
// left the depth buffer cleared — the sky, never on top of a wall.
void GameRenderer::draw_backgrounds(const av::Camera& camera) {
    const float view_aspect = static_cast<float>(width_) / static_cast<float>(std::max(1, height_));
    const float tan_half_fov = std::tan(camera.fov * kPi / 360.0f);
    constexpr float kOverscan = 1.06f;      // covers the parallax clamp, like the engine's
    constexpr float kBackgroundPlane = 11000.0f;   // the world Z background sprites sit on

    for (const auto& object : objects_) {
        if (!object.background_tex || object.live.hidden) continue;

        // Cover the view, preserving the sheet's own aspect ratio.
        float tex_aspect = view_aspect;
        auto found = texture_aspects_.find(object.background_tex);
        if (found != texture_aspects_.end() && found->second > 0.01f) tex_aspect = found->second;
        float half_x = 1.0f, half_y = 1.0f;
        if (tex_aspect >= view_aspect) half_x = tex_aspect / view_aspect;
        else                           half_y = view_aspect / tex_aspect;
        half_x *= kOverscan;
        half_y *= kOverscan;

        // Parallax: the object's own position relative to where the camera looks,
        // projected at the background plane and clamped into the overscan.
        const float ndc_per_world = 1.0f / std::max(1.0f, kBackgroundPlane * tan_half_fov);
        const float shift[2] = {
            std::clamp((object.live.pos_x - camera.target[0]) * ndc_per_world,
                       -(half_x - 1.0f), half_x - 1.0f),
            std::clamp((object.live.pos_y - camera.target[1]) * ndc_per_world,
                       -(half_y - 1.0f), half_y - 1.0f),
        };
        const float extent[2] = {half_x, half_y};
        const float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        pipeline_.draw_background(object.background_tex, shift, extent, color);
    }
}

// A unit quad in the XY plane: the mesh the water passes place and
// scale. Positions are +-1 so a caller's matrix columns become the quad's half
// extents, which is how the engine places a sprite.
void GameRenderer::build_quad_mesh() {
    av::PODMesh quad;
    quad.positions = {-1.0f, -1.0f, 0.0f,  1.0f, -1.0f, 0.0f,
                       1.0f,  1.0f, 0.0f, -1.0f,  1.0f, 0.0f};
    quad.normals   = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    quad.uvs       = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    quad.indices   = {0, 1, 2, 0, 2, 3};
    quad.num_vertices = 4;
    quad.num_faces    = 2;
    pipeline_.upload_mesh(quad, quad_mesh_, false);
}

// A 1x1 white texture, for materials that need a sample but own no image.
unsigned int GameRenderer::ensure_white_texture() {
    if (white_texture_) return white_texture_;
    const uint8_t pixel[4] = {255, 255, 255, 255};
    white_texture_ = upload_rgba(pixel, 1, 1);
    return white_texture_;
}

void GameRenderer::present() {
    if (window_) SDL_GL_SwapWindow(window_);
}

bool GameRenderer::save_screenshot(const std::string& path) {
    if (width_ <= 0 || height_ <= 0) return false;
    std::vector<uint8_t> pixels(static_cast<size_t>(width_) * height_ * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // glReadPixels is bottom-up; scanlines are flipped for the image writer.
    std::vector<uint8_t> flipped(static_cast<size_t>(width_) * height_ * 4);
    const size_t stride = static_cast<size_t>(width_) * 4;
    for (int y = 0; y < height_; ++y) {
        std::memcpy(&flipped[static_cast<size_t>(y) * stride],
                    &pixels[static_cast<size_t>(height_ - 1 - y) * stride], stride);
    }

    std::string lower = path;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".png") == 0) {
        SDL_Surface* surface = SDL_CreateSurfaceFrom(width_, height_, SDL_PIXELFORMAT_ABGR8888,
                                                     flipped.data(), static_cast<int>(stride));
        if (surface) {
            const bool ok = IMG_SavePNG(surface, path.c_str());
            SDL_DestroySurface(surface);
            return ok;
        }
        return false;
    }
    // PPM (P6) — no encoder dependency.
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    std::fprintf(file, "P6\n%d %d\n255\n", width_, height_);
    std::vector<uint8_t> rgb(static_cast<size_t>(width_) * height_ * 3);
    for (size_t i = 0, j = 0; i < flipped.size(); i += 4, j += 3) {
        rgb[j] = flipped[i];
        rgb[j + 1] = flipped[i + 1];
        rgb[j + 2] = flipped[i + 2];
    }
    std::fwrite(rgb.data(), 1, rgb.size(), file);
    std::fclose(file);
    return true;
}

} // namespace game
} // namespace caver
