#include "ruby/caver/engine.h"

#include <algorithm>

#include "ruby/caver/program_host.h"
#include "tools/scene_loader.h"

namespace caver {

const char* to_string(LiveEngineState state) {
    switch (state) {
        case LiveEngineState::Detached: return "detached";
        case LiveEngineState::Booting:  return "booting";
        case LiveEngineState::Running:  return "running";
        case LiveEngineState::Paused:   return "paused";
        case LiveEngineState::Stopped:  return "stopped";
        case LiveEngineState::Failed:   return "failed";
    }
    return "unknown";
}

namespace {

constexpr double kFixedStep = 1.0 / 60.0;
constexpr double kMaxCatchUp = 0.25;   // never simulate more than 250 ms per wake

} // namespace

// ── NativeBackend ───────────────────────────────────────────────────────────
bool NativeBackend::load(const av::SceneData& scene) {
    warnings_.clear();
    scene_.set_library_manager(libraries_);
    scene_.set_program_host(program_host_);
    scene_.set_hero(hero_);
    if (ground_query_) scene_.set_ground_query(ground_query_);
    const bool ok = scene_.load(scene);
    warnings_ = scene_.warnings();
    return ok;
}

// ── LiveEngineBackend ───────────────────────────────────────────────────────
// Thin forwarder: every method is the adapter's, so the host (EnginePod) keeps
// ownership of the process, the frame ring and the control channel.
bool LiveEngineBackend::load(const av::SceneData& scene) {
    warnings_.clear();
    render_.clear();
    components_ = 0;
    clock_ = 0.0;
    if (!engine_) {
        warnings_.push_back("live engine backend started without an ILiveEngine");
        return false;
    }
    std::string error;
    std::string stem = scene_stem_;
    if (stem.empty()) stem = scene.filename;              // "town_part1.scene"
    if (stem.size() > 6 && stem.compare(stem.size() - 6, 6, ".scene") == 0)
        stem = stem.substr(0, stem.size() - 6);
    if (!engine_->attach(LiveEngineConfig{}, &error)) {
        warnings_.push_back("live engine attach failed: " + error);
        return false;
    }
    if (!stem.empty() && !engine_->load_scene(stem, &error)) {
        warnings_.push_back("live engine scene load failed: " + error);
        return false;
    }
    std::vector<std::string> notes = engine_->notes();
    warnings_.insert(warnings_.end(), notes.begin(), notes.end());
    return true;
}

void LiveEngineBackend::update(float dt) {
    if (!engine_) return;
    engine_->set_paused(false);
    engine_->step(static_cast<double>(dt));
    clock_ += dt;
    std::vector<ObjectRenderState> readback;
    if (engine_->read_state(readback)) {
        render_ = std::move(readback);
        components_ = 0;
        for (const ObjectRenderState& state : render_) (void)state;
    }
}

bool Engine::start(Backend backend, ILiveEngine* live) {
    if (running_.load()) return true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        backend_kind_ = backend;
        live_ = live;
        if (backend == Backend::LiveEngine) {
            auto adapter = std::make_unique<LiveEngineBackend>(live);
            backend_ = std::move(adapter);
        } else {
            auto native = std::make_unique<NativeBackend>();
            native->set_program_host(program_host_);
            native->set_library_manager(libraries_);
            native->set_hero(hero_);
            if (ground_query_) native->set_ground_query(ground_query_);
            backend_ = std::move(native);
        }
        snapshot_ = Snapshot{};
    }
    stop_.store(false);
    running_.store(true);
    worker_ = std::thread(&Engine::run, this);
    return true;
}

NativeBackend* Engine::native() {
    std::lock_guard<std::mutex> lock(mutex_);
    return dynamic_cast<NativeBackend*>(backend_.get());
}

const RuntimeScene* Engine::runtime_scene() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto* native = dynamic_cast<NativeBackend*>(backend_.get())) return &native->scene();
    return nullptr;
}

void Engine::set_live_scene(const std::string& scene_stem) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto* live = dynamic_cast<LiveEngineBackend*>(backend_.get())) live->set_scene_stem(scene_stem);
}

void Engine::stop() {
    if (!running_.exchange(false)) {
        if (worker_.joinable()) worker_.join();
        return;
    }
    stop_.store(true);
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    backend_.reset();
    pending_scene_.reset();
    has_pending_ = false;
}

void Engine::set_scene(av::SceneData scene) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_scene_ = std::make_unique<av::SceneData>(std::move(scene));
        has_pending_ = true;
    }
    wake_.notify_all();
}

bool Engine::has_scene() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_pending_ || snapshot_.loaded;
}

Snapshot Engine::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

std::vector<std::string> Engine::unrecovered_classes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto* native = dynamic_cast<NativeBackend*>(backend_.get()))
        return native->scene().unrecovered_classes();
    return {};
}

void Engine::publish(Snapshot&& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = std::move(snapshot);
}

void Engine::run() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    double accumulator = 0.0;

    // FPS sampling.
    auto fps_window_start = last;
    int  frames = 0;

    while (!stop_.load()) {
        // 1. Adopt a staged scene, if any.
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (has_pending_) {
                if (backend_) {
                    const bool ok = backend_->load(*pending_scene_);
                    Snapshot fresh;
                    fresh.loaded = ok;
                    fresh.render = backend_->render_state();
                    fresh.warnings = backend_->warnings();
                    fresh.clock = backend_->clock();
                    fresh.objects = backend_->object_count();
                    fresh.components = backend_->component_count();
                    if (backend_kind_ == Backend::LiveEngine && live_) {
                        fresh.notes = live_->notes();
                        fresh.notes.insert(fresh.notes.begin(),
                                           std::string("live engine: ") + to_string(live_->state()));
                    } else {
                        fresh.notes.push_back("recovered runtime (caver::RuntimeScene)");
                    }
                    if (program_host_) fresh.running_scripts = program_host_->running_scripts();
                    snapshot_ = std::move(fresh);
                }
                pending_scene_.reset();
                has_pending_ = false;
                accumulator = 0.0;
                last = clock::now();
            }
        }

        const bool paused = paused_.load();

        // 2. Advance simulated time by a fixed step, scaled, with catch-up cap.
        const auto now = clock::now();
        double elapsed = std::chrono::duration<double>(now - last).count();
        last = now;
        if (elapsed > kMaxCatchUp) elapsed = kMaxCatchUp;
        if (!paused) accumulator += elapsed * static_cast<double>(time_scale_.load());

        bool stepped = false;
        while (accumulator >= kFixedStep) {
            accumulator -= kFixedStep;
            std::lock_guard<std::mutex> lock(mutex_);
            if (backend_) {
                backend_->update(static_cast<float>(kFixedStep));
                stepped = true;
            }
        }

        if (stepped) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (backend_) {
                    Snapshot frame;
                    frame.loaded = snapshot_.loaded;
                    frame.render = backend_->render_state();
                    frame.warnings = backend_->warnings();
                    frame.clock = backend_->clock();
                    frame.frame = snapshot_.frame + 1;
                    frame.objects = backend_->object_count();
                    frame.components = backend_->component_count();
                    frame.notes = snapshot_.notes;
                    if (program_host_) frame.running_scripts = program_host_->running_scripts();
                    snapshot_ = std::move(frame);
                }
            }
            ++frames;
        }

        // 3. Publish FPS roughly twice a second.
        if (std::chrono::duration<double>(now - fps_window_start).count() >= 0.5) {
            fps_.store(frames / std::chrono::duration<double>(now - fps_window_start).count());
            frames = 0;
            fps_window_start = now;
        }

        // 4. Sleep until the next frame (or a wake-up from set_scene/stop).
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(paused ? 16 : 4),
                       [this] { return stop_.load() || has_pending_; });
    }
}

} // namespace caver
