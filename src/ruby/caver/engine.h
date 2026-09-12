#pragma once
// engine.h — the preview engine: one worker thread, one backend, published frames.
//
// Two backends produce the same `ObjectRenderState` stream the renderer consumes:
//
//   Native      — caver::RuntimeScene, our own recovered simulation. Fast,
//                 headless, per-component inspectable; this is what the SCL
//                 studio, lint and the 3D diorama drive.
//   LiveEngine  — the genuine engine, but never run from here: the backend
//                 forwards to an `ILiveEngine` supplied by the host application,
//                 which in Ruby is the existing EnginePod / swordfare pod path
//                 (see live_engine.h). caver owns no emulator runner.
//
// The engine owns the backend and its thread, stepping at a fixed rate and
// publishing immutable snapshots. The UI thread only ever copies a snapshot, so
// previewing never blocks the editor, and the viewport is not touched — it
// applies snapshots the same way it applies scene edits.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ruby/caver/live_engine.h"
#include "ruby/caver/runtime.h"

namespace caver {

enum class Backend {
    Native,        // caver's own recovered runtime
    LiveEngine,    // the real engine, reached through an injected ILiveEngine
};

// Common shape of a preview backend.
class IBackend {
public:
    virtual ~IBackend() = default;
    virtual bool load(const av::SceneData& scene) = 0;
    virtual void update(float dt) = 0;
    virtual const std::vector<ObjectRenderState>& render_state() const = 0;
    virtual double clock() const = 0;
    virtual const std::vector<std::string>& warnings() const = 0;
    virtual size_t component_count() const = 0;
    virtual size_t object_count() const = 0;
};

// caver's own recovered simulation.
class NativeBackend : public IBackend {
public:
    bool load(const av::SceneData& scene) override;
    void update(float dt) override { scene_.update(dt); }
    const std::vector<ObjectRenderState>& render_state() const override { return scene_.render_state(); }
    double clock() const override { return scene_.clock(); }
    const std::vector<std::string>& warnings() const override { return warnings_; }
    size_t component_count() const override { return scene_.component_count(); }
    size_t object_count() const override { return scene_.objects().size(); }
    const RuntimeScene& scene() const { return scene_; }
    RuntimeScene& mutable_scene() { return scene_; }
    // The SCL runtime + scripting host the caller wired in before load().
    void set_library_manager(LibraryManager* libraries) { libraries_ = libraries; }
    void set_program_host(ProgramHost* host) { program_host_ = host; }
    void set_hero(const std::string& identifier) { hero_ = identifier; }
    void set_ground_query(std::function<float(float, float)> query) { ground_query_ = std::move(query); }
private:
    RuntimeScene scene_;
    std::vector<std::string> warnings_;
    LibraryManager* libraries_ = nullptr;
    ProgramHost*    program_host_ = nullptr;
    std::string     hero_ = "hero";
    std::function<float(float, float)> ground_query_;
};

// The real engine, forwarded to the host-owned adapter (EnginePod). It never
// spawns or emulates anything itself.
class LiveEngineBackend : public IBackend {
public:
    explicit LiveEngineBackend(ILiveEngine* engine) : engine_(engine) {}

    bool load(const av::SceneData& scene) override;
    void update(float dt) override;
    const std::vector<ObjectRenderState>& render_state() const override { return render_; }
    double clock() const override { return clock_; }
    const std::vector<std::string>& warnings() const override { return warnings_; }
    size_t component_count() const override { return components_; }
    size_t object_count() const override { return render_.size(); }
    void set_scene_stem(const std::string& stem) { scene_stem_ = stem; }

private:
    ILiveEngine* engine_ = nullptr;
    std::string  scene_stem_;
    std::vector<ObjectRenderState> render_;
    std::vector<std::string> warnings_;
    double clock_ = 0.0;
    size_t components_ = 0;
};

// A published frame of simulation output. Copies are cheap and lock-scoped.
struct Snapshot {
    std::vector<ObjectRenderState> render;
    std::vector<std::string> warnings;
    std::vector<std::string> notes;      // backend status lines (live engine)
    double   clock = 0.0;
    uint64_t frame = 0;
    size_t   objects = 0;
    size_t   components = 0;
    int      running_scripts = 0;
    bool     loaded = false;
};

class Engine {
public:
    Engine() = default;
    ~Engine() { stop(); }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Spawn the worker thread. `live` is required for Backend::LiveEngine and
    // ignored otherwise: the host application owns it (Ruby passes its
    // EnginePod-backed adapter, so there is exactly one engine runner).
    bool start(Backend backend = Backend::Native, ILiveEngine* live = nullptr);
    void stop();
    bool running() const { return running_.load(); }
    Backend backend() const { return backend_kind_; }

    // Access the native backend so the caller can wire the SCL runtime, the
    // scripting host and the ground query before loading a scene. Safe before
    // start(); returns nullptr for other backends.
    NativeBackend* native();

    // ── native-backend wiring (set before start()) ──────────────────────────
    // The Lua 5.1 host the native backend drives (owned by the caller).
    void set_program_host(ProgramHost* host) { program_host_ = host; }
    // The SCL registry scripted spawns resolve through (owned by the caller:
    // the editor's registry is shared, so an edited .scl is live for preview).
    void set_library_manager(LibraryManager* libraries) { libraries_ = libraries; }
    // The object scripts address as the hero.
    void set_hero(const std::string& identifier) { hero_ = identifier; }
    // World grounding from the av:: collision layer (x, z) -> ground height.
    void set_ground_query(std::function<float(float, float)> query) { ground_query_ = std::move(query); }

    // Thread-safe: stages a scene for the worker to load on its next step.
    void set_scene(av::SceneData scene);
    bool has_scene() const;
    // The scene stem a live engine should load.
    void set_live_scene(const std::string& scene_stem);

    void set_paused(bool paused);
    bool paused() const { return paused_.load(); }
    void set_time_scale(float scale) { time_scale_.store(scale); }

    // Latest published frame (copied under the snapshot lock).
    Snapshot snapshot() const;
    double fps() const { return fps_.load(); }

    // The backend's recovered-component to-do list, from the last loaded scene.
    std::vector<std::string> unrecovered_classes() const;
    // The recovered runtime behind the native backend (nullptr otherwise). Only
    // safe to read while paused/stopped; the studio uses it for inspection.
    const RuntimeScene* runtime_scene() const;

private:
    void run();
    void publish(Snapshot&& snapshot);

    mutable std::mutex      mutex_;          // guards snapshot_, pending_scene_, backend_
    std::condition_variable wake_;
    Snapshot                snapshot_;
    std::unique_ptr<IBackend> backend_;
    std::unique_ptr<av::SceneData> pending_scene_;
    bool                    has_pending_ = false;
    Backend                 backend_kind_ = Backend::Native;
    ILiveEngine*            live_ = nullptr;
    ProgramHost*            program_host_ = nullptr;
    LibraryManager*         libraries_ = nullptr;
    std::string             hero_ = "hero";
    std::function<float(float, float)> ground_query_;

    std::thread             worker_;
    std::atomic<bool>       running_{false};
    std::atomic<bool>       stop_{false};
    std::atomic<bool>       paused_{false};
    std::atomic<float>      time_scale_{1.0f};
    std::atomic<double>     fps_{0.0};
};

} // namespace caver
