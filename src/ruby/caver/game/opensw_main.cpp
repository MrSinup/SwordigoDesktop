// opensw — boot Swordigo's own game runtime, recovered, with no menu and no save
// selection.
//
// WHAT THIS IS
// ------------
// Not an emulator and not a libswordigo host: this is `caver::`, Ruby's recovered
// engine, running the shipped game data through the real boot chain. The chain is
// the engine's own, read off arm32_13:
//
//   1. PlayerProfile::CreateProfile("newplayer")
//        <id>.gstate -> Proto::GameState, plus test.scmap and gamedata.gdata
//        ("normal / 0 progress" is literally newplayer.gstate: CurrentLevel =
//        town_herohouse, CurrentSpawnPoint = spawn_default, empty CharacterState)
//   2. GameSceneController::InitWithScene   — load <CurrentLevel>.scene
//   3. GameSceneController::SpawnHeroAt     — the spawn point object, else spawn_default
//   4. GameSceneController::CreateHeroObjectAt — ObjectLibrary "hiro", template "hiro",
//                                                identifier "hero", health 2*level+4
//   5. GameSceneController::AddHeroObjectToScene — equipment from the save
//   6. GameSceneController::Update           — scene update, collision, Lua programs
//
// Nothing about the world, the level list, the spawn point, the hero or its speed
// is written down in this file: every value is read from the shipped data.
//
// WHAT IT DRAWS WITH
// ------------------
// The window is the studio's own render stack, not a second renderer:
// av::scene_load for the level, av::assets::resolve_pod / av::pod_load for its
// models, swk::object_render_matrix for the transforms, av::render_mesh /
// render_water_sheet / render_background_quad / render_fire_sprite /
// render_shadow_blob for the draw, av::postfx_apply for the grade, and the
// engine's own camera (InitWithScene + CameraController). The hero is drawn out
// of the archetype the ObjectLibrary gave the runtime, so its model is whatever
// the `hiro` template says. Input is keyboard -> GameControlButton, the same
// enum the touch layer feeds.
//
// USAGE
//   opensw [--profile player] [--level NAME|save] [--frames N] [--fps N]
//          [--assets DIR] [--input SPEC] [--headless] [--no-postfx] [--size WxH]
//          [--shot FILE] [--shot-frame N] [--quiet]
//
//   --level NAME   open this level (default: town_part1; `save` = the profile's
//                  own CurrentLevel, which is what the engine itself loads)
//   --frames N     run N frames then exit (default: until the window closes)
//   --input SPEC   scripted input, e.g. "0-59:right,60-89:right+jump,90-120:attack"
//   --assets DIR   sets the asset root the game reads (g_instance_assets_dir)
//   --headless     simulate without a window
//   --shot FILE    render one frame to FILE (.png/.ppm) — works headless too

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "platform/data_path.h"
#include "ruby/caver/game/game_control.h"
#include "ruby/caver/game/game_renderer.h"
#include "ruby/caver/game/game_scene_controller.h"
#include "ruby/caver/game/player_profile.h"
#include <SDL3/SDL.h>

// The asset root the platform data path prepends to "resources/". data_path.cpp
// declares it weak; like the other executables in this repo, OpenSwordigo
// provides the strong definition so --assets can pick a build at run time.
std::string g_instance_assets_dir = "assets";

namespace {

struct Options {
    std::string profile = "player";     // the save slot the game would use
    // The shell boots the town by default so a bare `./opensw` shows the game;
    // `--level save` follows the profile's own CurrentLevel instead, and
    // `--level NAME` opens that level.
    std::string level = "town_part1";
    std::string template_state = "newplayer";  // the 0-progress template
    std::string assets;
    std::string input_spec;
    int  frames = -1;
    int  fps = 60;
    bool quiet = false;
    bool help = false;
    // Rendering (on by default — the shell draws the level with the studio's
    // own render stack and its own recovered runtime).
    bool render = true;
    bool postfx = true;
    bool visible = true;
    int  width  = 1280;
    int  height = 720;
    std::string shot;       // write this frame to disk (png/ppm) and exit
    int  shot_frame = 0;
    unsigned layers = 0x7fu; // GameRenderer draw-pass mask
    bool dump_render = false; // print every parsed render object and exit
    std::string camera_override; // "X,Y,DIST,PITCH[,YAW]"
    // Diagnostics: boot the level without the hero actor, so a frame with and a
    // frame without Hiro can be diffed to prove where the actor is drawn.
    bool no_hero = false;
};

void print_usage() {
    std::printf(
        "opensw — boot Swordigo's recovered game runtime straight into a level\n\n"
        "  --profile NAME     save slot to use (default: player)\n"
        "  --new              start from a fresh hero (newplayer.gstate)\n"
        "  --level NAME       override the level from the save (e.g. town_part1)\n"
        "  --frames N         stop after N frames\n"
        "  --fps N            simulation rate (default 60)\n"
        "  --assets DIR       asset root (g_instance_assets_dir)\n"
        "  --input SPEC       scripted input, e.g. \"0-59:right,60-89:right+jump\"\n"
        "  --level save       follow the profile's own CurrentLevel\n"
        "  --headless         do not open a window (simulation only)\n"
        "  --no-postfx        render without the HDR/bloom/grade pipeline\n"
        "  --size WxH         window size (default 1280x720)\n"
        "  --shot FILE        write frame --shot-frame to FILE (.png/.ppm) and exit\n"
        "  --shot-frame N     frame to capture (default 0)\n"
        "  --quiet            suppress the per-frame state line\n");
}

// One scripted input segment: frames [from, to] holding a set of buttons.
struct InputSegment {
    int from = 0;
    int to = 0;
    std::vector<caver::game::GameControlButton> buttons;
};

caver::game::GameControlButton button_from_name(const std::string& name) {
    using caver::game::GameControlButton;
    if (name == "left")   return GameControlButton::MoveLeft;
    if (name == "right")  return GameControlButton::MoveRight;
    if (name == "jump")   return GameControlButton::Jump;
    if (name == "attack") return GameControlButton::Attack;
    if (name == "use")    return GameControlButton::Use;
    if (name == "cast")   return GameControlButton::CastSkill;
    if (name == "talk")   return GameControlButton::Talk;
    if (name == "portal") return GameControlButton::EnterPortal;
    return GameControlButton::None;
}

// "0-59:right,60-89:right+jump" -> segments. A plain "right" applies to all frames.
std::vector<InputSegment> parse_input(const std::string& spec) {
    std::vector<InputSegment> segments;
    if (spec.empty()) return segments;
    size_t start = 0;
    while (start <= spec.size()) {
        size_t comma = spec.find(',', start);
        std::string part = spec.substr(start, comma == std::string::npos ? std::string::npos
                                                                         : comma - start);
        start = comma == std::string::npos ? spec.size() + 1 : comma + 1;
        if (part.empty()) continue;

        InputSegment segment;
        segment.to = -1;   // open ended until a range is parsed
        size_t colon = part.find(':');
        if (colon != std::string::npos) {
            std::string range = part.substr(0, colon);
            part = part.substr(colon + 1);
            size_t dash = range.find('-');
            segment.from = std::atoi(range.c_str());
            segment.to = dash == std::string::npos ? segment.from : std::atoi(range.c_str() + dash + 1);
        }
        size_t plus = 0;
        while (plus <= part.size()) {
            size_t next = part.find('+', plus);
            std::string name = part.substr(plus, next == std::string::npos ? std::string::npos
                                                                          : next - plus);
            if (!name.empty()) segment.buttons.push_back(button_from_name(name));
            plus = next == std::string::npos ? part.size() + 1 : next + 1;
        }
        segments.push_back(std::move(segment));
    }
    return segments;
}

bool segment_active(const std::vector<InputSegment>& segments, int frame,
                    std::vector<caver::game::GameControlButton>* out) {
    out->clear();
    for (const auto& segment : segments) {
        const bool in_range = frame >= segment.from && (segment.to < 0 || frame <= segment.to);
        if (!in_range) continue;
        for (auto button : segment.buttons) out->push_back(button);
    }
    return !out->empty();
}

// "bg,objects,actors,shadows,lights,water" -> GameRenderer layer mask.
// Empty or "all" means every pass.
unsigned parse_layers(const std::string& spec) {
    if (spec.empty() || spec == "all") return 0x3fu;
    unsigned mask = 0;
    size_t start = 0;
    while (start <= spec.size()) {
        size_t comma = spec.find(',', start);
        const std::string name = spec.substr(start, comma == std::string::npos ? std::string::npos
                                                                              : comma - start);
        if (name == "bg" || name == "backgrounds") mask |= caver::game::GameRenderer::kLayerBackgrounds;
        else if (name == "objects") mask |= caver::game::GameRenderer::kLayerObjects;
        else if (name == "actors") mask |= caver::game::GameRenderer::kLayerActors;
        else if (name == "shadows") mask |= caver::game::GameRenderer::kLayerShadows;
        else if (name == "lights") mask |= caver::game::GameRenderer::kLayerLights;
        else if (name == "water") mask |= caver::game::GameRenderer::kLayerWater;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return mask;
}

bool parse_args(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& into) { if (i + 1 < argc) into = argv[++i]; };
        if (arg == "--help" || arg == "-h") { options.help = true; return true; }
        else if (arg == "--profile") next(options.profile);
        else if (arg == "--new") options.profile = options.template_state;
        else if (arg == "--level") next(options.level);
        else if (arg == "--assets") next(options.assets);
        else if (arg == "--input") next(options.input_spec);
        else if (arg == "--frames") { std::string v; next(v); options.frames = std::atoi(v.c_str()); }
        else if (arg == "--fps") { std::string v; next(v); options.fps = std::atoi(v.c_str()); }
        else if (arg == "--headless") options.render = false;
        else if (arg == "--no-postfx") options.postfx = false;
        // --layers bg,objects,actors,shadows,lights,water — draw-pass mask, for
        // bisecting a wrong frame down to the pass that drew it.
        else if (arg == "--layers") { std::string v; next(v); options.layers = parse_layers(v); }
        else if (arg == "--hidden") options.visible = false;
        else if (arg == "--no-hero") options.no_hero = true;
        else if (arg == "--dump-render") options.dump_render = true;
        // --cam X,Y,DIST,PITCH[,YAW] — spectator camera, overriding the
        // GameSceneController frame. Purely a preview/debug aid.
        else if (arg == "--cam") { std::string v; next(v); options.camera_override = v; }
        else if (arg == "--shot") next(options.shot);
        else if (arg == "--shot-frame") { std::string v; next(v); options.shot_frame = std::atoi(v.c_str()); }
        else if (arg == "--size") {
            std::string v; next(v);
            const size_t x = v.find('x');
            if (x != std::string::npos) {
                options.width  = std::atoi(v.substr(0, x).c_str());
                options.height = std::atoi(v.substr(x + 1).c_str());
            }
        }
        else if (arg == "--quiet") options.quiet = true;
        else { std::fprintf(stderr, "opensw: unknown argument '%s'\n", arg.c_str()); return false; }
    }
    if (options.fps <= 0) options.fps = 60;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_args(argc, argv, options)) return 2;
    if (options.help) { print_usage(); return 0; }

    if (!options.assets.empty()) g_instance_assets_dir = options.assets;

    using namespace caver::game;

    // ── 1. the profile ──────────────────────────────────────────────────────
    PlayerProfile profile;
    const std::string local_path = PlayerProfile::local_file_path(options.profile);
    bool loaded = false;
    if (PlayerProfile::profile_exists(options.profile)) {
        loaded = profile.load_from_file(local_path);
        if (!loaded)
            std::fprintf(stderr, "opensw: %s exists but failed to parse; recreating\n",
                         local_path.c_str());
    }
    if (!loaded) {
        // No save yet: the game creates the profile from the 0-progress template,
        // under the profile's own id.
        if (!profile.create_profile(options.template_state, options.profile)) {
            std::fprintf(stderr,
                         "opensw: could not read %s — check --assets and the data path\n",
                         PlayerProfile::resource_path(options.template_state, "gstate").c_str());
            return 1;
        }
        std::printf("opensw: created profile '%s' from %s.gstate\n", options.profile.c_str(),
                    options.template_state.c_str());
        std::printf("opensw: wrote %s\n", PlayerProfile::local_file_path(options.profile).c_str());
    } else {
        std::printf("opensw: loaded profile '%s' from %s\n", profile.identifier().c_str(),
                    local_path.c_str());
    }

    // ── 2. the level ────────────────────────────────────────────────────────
    // `--level save` is the engine's own behaviour (GameSceneController loads
    // GameState::CurrentLevel); the profile we just created names
    // town_herohouse. The bare default is town_part1 — the town itself, which
    // is where a playable session actually shows something.
    const bool follow_save = options.level.empty() || options.level == "save";
    std::string level = follow_save ? profile.state().current_level() : options.level;
    if (level.empty()) {
        std::fprintf(stderr, "opensw: the save names no current level\n");
        return 1;
    }
    const std::string spawn = follow_save ? profile.state().current_spawn_point() : std::string();

    // ── 3. the scene controller ─────────────────────────────────────────────
    GameSceneController game;
    std::string error;
    if (!game.init_with_scene(profile, level, &error)) {
        std::fprintf(stderr, "opensw: %s\n", error.c_str());
        return 1;
    }

    std::printf("opensw: level '%s' (%s)\n", level.c_str(), game.level_title().c_str());
    std::printf("opensw: %zu objects, %zu components, %zu libraries, %zu collision shapes\n",
                game.scene_data().objects.size(), game.component_count(),
                game.libraries().template_count(), game.collision_world().shape_count());
    std::printf("opensw: collision world: %zu active shapes\n",
                game.collision_world().active_shape_count());

    std::printf("opensw: %zu embedded Lua programs in the level (handlers + loops)\n",
                game.runtime().programs().size());

    if (!game.spawn_hero_at(spawn)) {
        std::fprintf(stderr, "opensw: hero spawn failed (spawn point '%s')\n", spawn.c_str());
        for (const auto& note : game.notes()) std::fprintf(stderr, "  %s\n", note.c_str());
        return 1;
    }

    const auto& tuning = game.hero_tuning();
    std::printf("opensw: hero at (%.1f, %.1f, %.1f) via spawn point '%s'\n",
                game.hero()->pos[0], game.hero()->pos[1], game.hero()->pos[2],
                game.spawn_point_used().c_str());
    std::printf("opensw: hero tuning run=%.1f jump=%.1f maxJumpTime=%.2f gravity=%.1f radius=%.1f\n",
                tuning.run_speed, tuning.jump_speed, tuning.max_jump_time, tuning.gravity_magnitude,
                game.hero_radius());
    for (const auto& note : game.notes()) std::printf("opensw: %s\n", note.c_str());
    for (const auto& warning : game.warnings()) std::printf("opensw: warning: %s\n", warning.c_str());

    const std::vector<InputSegment> script = parse_input(options.input_spec);

    // ── 4. the renderer ────────────────────────────────────────────────────
    // The level is drawn with the studio's own stack (av:: scene loader +
    // av_renderer + the shared workspace transforms), and the hero is drawn out
    // of the archetype the ObjectLibrary gave the runtime — so nothing about
    // how the town or Hiro looks is written down here.
    caver::game::GameRenderer renderer;
    caver::game::GameRenderer::Options render_options;
    render_options.width   = options.width;
    render_options.height  = options.height;
    render_options.visible = options.visible;
    render_options.postfx  = options.postfx;
    render_options.layers  = options.layers;
    render_options.title   = "opensw — " + level;

    bool rendering = false;
    int  hero_actor = -1;
    int  hero_index = -1;
    if (options.render) {
        if (!renderer.init(render_options)) {
            std::fprintf(stderr, "opensw: %s\n", renderer.last_error().c_str());
            std::fprintf(stderr, "opensw: continuing headless\n");
        } else {
            rendering = true;
            if (!renderer.build_level(game.scene_data())) {
                std::fprintf(stderr, "opensw: %s\n", renderer.last_error().c_str());
            }
            for (const auto& warning : renderer.warnings())
                std::fprintf(stderr, "opensw: render warning: %s\n", warning.c_str());
            if (options.dump_render) {
                renderer.dump_objects();
                return 0;
            }

            if (const av::SceneObject* archetype = options.no_hero ? nullptr : game.hero_archetype()) {
                const size_t warnings_before = renderer.warnings().size();
                hero_actor = renderer.add_actor("hero", *archetype);
                if (caver::game::RenderActor* actor = renderer.actor(hero_actor)) {
                    actor->hitbox_width = game.hero_radius() * 2.0f;
                    // The hero's feet sit at the CollisionShape's local min-Y, not
                    // at the object origin (the physics rests the shape, not the
                    // model, on the ground).
                    actor->feet_offset = game.hero_feet_offset();
                }
                std::printf("opensw: hero archetype '%s' -> model '%s' (template scaling %.2f)\n",
                            game.hero_template_name().c_str(), archetype->mesh_name.c_str(),
                            game.hero_template_scaling());
                const auto& hero_warnings = renderer.warnings();
                for (size_t i = warnings_before; i < hero_warnings.size(); ++i)
                    std::fprintf(stderr, "opensw: hero render warning: %s\n",
                                 hero_warnings[i].c_str());
            } else if (!options.no_hero) {
                std::fprintf(stderr, "opensw: no hero archetype resolved; Hiro will not be drawn\n");
            }
        }
    }

    // The hero is appended after the level's own objects, so its render state
    // sits at that index.
    {
        const auto& objects = game.runtime().objects();
        for (size_t i = 0; i < objects.size(); ++i) {
            if (objects[i].identifier == "hero") { hero_index = static_cast<int>(i); break; }
        }
    }

    // ── 5. the update loop ─────────────────────────────────────────────────
    const float dt = 1.0f / static_cast<float>(options.fps);
    std::vector<GameControlButton> held;
    int frame = 0;
    const auto started = std::chrono::steady_clock::now();
    auto last_step = started;
    double step_accumulator = 0.0;
    auto last_report = started;

    const std::function<std::vector<GameControlButton>()> read_keys = [&] {
        std::vector<GameControlButton> active;
        auto held_key = [&](int scancode) { return renderer.key_down(scancode); };
        if (held_key(SDL_SCANCODE_LEFT) || held_key(SDL_SCANCODE_A)) active.push_back(GameControlButton::MoveLeft);
        if (held_key(SDL_SCANCODE_RIGHT) || held_key(SDL_SCANCODE_D)) active.push_back(GameControlButton::MoveRight);
        if (held_key(SDL_SCANCODE_SPACE) || held_key(SDL_SCANCODE_Z)) active.push_back(GameControlButton::Jump);
        if (held_key(SDL_SCANCODE_X) || held_key(SDL_SCANCODE_K)) active.push_back(GameControlButton::Attack);
        if (held_key(SDL_SCANCODE_C) || held_key(SDL_SCANCODE_E)) active.push_back(GameControlButton::Use);
        if (held_key(SDL_SCANCODE_V) || held_key(SDL_SCANCODE_Q)) active.push_back(GameControlButton::CastSkill);
        if (held_key(SDL_SCANCODE_UP) || held_key(SDL_SCANCODE_W)) active.push_back(GameControlButton::EnterPortal);
        if (held_key(SDL_SCANCODE_T)) active.push_back(GameControlButton::Talk);
        return active;
    };

    bool quit = false;
    for (;;) {
        if (options.frames >= 0 && frame >= options.frames) break;
        if (quit) break;

        if (rendering && !renderer.pump_events()) break;

        // Scripted input drives the same GameControlButtonDown/Up API a pad or a
        // touch would, so nothing about the control path is special-cased here.
        std::vector<GameControlButton> active;
        if (!script.empty()) {
            segment_active(script, frame, &active);
        } else if (rendering) {
            active = read_keys();
        }
        for (auto button : held)
            if (std::find(active.begin(), active.end(), button) == active.end())
                game.game_control_button_up(button);
        for (auto button : active)
            if (std::find(held.begin(), held.end(), button) == held.end())
                game.game_control_button_down(button);
        held = active;

        game.update(dt);

        if (rendering) {
            // Level objects follow the simulation (elevators, script-moved
            // props, animated torches); the hero is drawn from its archetype.
            const auto& states = game.runtime().render_state();
            for (size_t i = 0; i < states.size(); ++i) {
                const auto& state = states[i];
                renderer.sync_object(static_cast<int>(i), state.pos, state.rot_y,
                                     state.scale, state.anim_time, state.hidden);
            }
            if (hero_actor >= 0 && hero_index >= 0 &&
                static_cast<size_t>(hero_index) < states.size()) {
                const auto& state = states[static_cast<size_t>(hero_index)];
                const caver::RuntimeObject* hero_object = game.hero();
                renderer.set_actor(hero_actor, state.pos, state.rot_y,
                                   hero_object ? static_cast<float>(hero_object->facing) : 1.0f,
                                   1.0f, state.anim_time, state.hidden);
                // Whatever clip the CharAnimController selected this frame
                // (stand / run / jump / fall / cast …) plays on its own pod.
                renderer.set_actor_clip(hero_actor, state.anim_name, state.anim_time);
            }

            av::Camera frame_camera = game.camera();
            if (!options.camera_override.empty()) {
                float v[5] = {frame_camera.target[0], frame_camera.target[1], frame_camera.distance,
                              frame_camera.pitch, frame_camera.yaw};
                std::sscanf(options.camera_override.c_str(), "%f,%f,%f,%f,%f",
                            &v[0], &v[1], &v[2], &v[3], &v[4]);
                frame_camera.target[0] = v[0];
                frame_camera.target[1] = v[1];
                frame_camera.distance   = v[2];
                frame_camera.pitch      = v[3];
                frame_camera.yaw        = v[4];
            }
            renderer.render(frame_camera, static_cast<float>(game.clock()));

            // Capture BEFORE the swap: glReadPixels(GL_BACK) after
            // SDL_GL_SwapWindow reads an undefined buffer (black on a visible
            // window, stale garbage on a hidden one).
            if (!options.shot.empty() && frame == options.shot_frame) {
                if (renderer.save_screenshot(options.shot))
                    std::printf("opensw: wrote %s (%dx%d)\n", options.shot.c_str(),
                                renderer.width(), renderer.height());
                else
                    std::fprintf(stderr, "opensw: could not write %s\n", options.shot.c_str());
                if (options.frames < 0) break;
            }

            renderer.present();
        }

        if (!options.quiet && (frame % options.fps == 0 || options.frames >= 0)) {
            const caver::RuntimeObject* hero = game.hero();
            std::printf("t=%6.2fs  hero=(%8.1f,%8.1f,%8.1f)  vel=(%7.1f,%7.1f)  "
                        "hp=%3.0f/%-3.0f  action=%d  anim=%s@%.2f  scripts=%d  contacts=%zu/%zu\n",
                        game.clock(), hero->pos[0], hero->pos[1], hero->pos[2],
                        hero->vel[0], hero->vel[1],
                        hero->behaviour.health.health, hero->behaviour.health.max_health,
                        static_cast<int>(hero->behaviour.action),
                        hero->behaviour.anim.current.empty() ? "-" : hero->behaviour.anim.current.c_str(),
                        hero->behaviour.anim.time, game.scripts_running(),
                        game.collision_world().last_contact_count(),
                        game.collision_world().update_calls());
        }

        // Steady-state report: what the frame actually cost and drew. Driven by
        // frames (not wall time) so scripted/headless runs report too.
        if (rendering && !options.quiet && frame > 0 && frame % 120 == 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto& stats = renderer.stats();
            std::printf("opensw: %.0f fps  draws=%d  triangles=%d  objects=%d  actors=%d  cpu=%.2fms\n",
                        frame / std::chrono::duration<double>(now - started).count(),
                        stats.draw_calls, stats.triangles, stats.objects,
                        stats.actors, stats.cpu_ms);
            last_report = now;
        }

        ++frame;

        if (!rendering && options.frames < 0 && script.empty()) {
            // Headless with no script and no frame budget: nothing to wait for.
            break;
        }
        if (rendering && options.frames < 0 && options.shot.empty()) {
            // Windowed interactive run: pace the fixed step against the wall
            // clock so the simulation runs at the engine's rate, not the GPU's.
            const auto now = std::chrono::steady_clock::now();
            step_accumulator += std::chrono::duration<double>(now - last_step).count();
            last_step = now;
            if (step_accumulator < dt) SDL_Delay(static_cast<Uint32>((dt - step_accumulator) * 1000.0));
            step_accumulator = 0.0;
        }
    }

    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started).count();
    const caver::RuntimeObject* hero = game.hero();
    std::printf("opensw: %d frames in %.2fs (%.0f frames/s)\n", frame, seconds,
                seconds > 0.0 ? frame / seconds : 0.0);
    std::printf("opensw: hero ended at (%.1f, %.1f, %.1f) hp=%.0f/%.0f action=%d\n",
                hero->pos[0], hero->pos[1], hero->pos[2], hero->behaviour.health.health,
                hero->behaviour.health.max_health, static_cast<int>(hero->behaviour.action));
    if (rendering) renderer.shutdown();
    return 0;
}
