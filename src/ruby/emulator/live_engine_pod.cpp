#include "ruby/emulator/live_engine_pod.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>

#include "ruby/emulator/engine_pod.h"

namespace ruby::emulator {

PodLiveEngine::PodLiveEngine(EnginePod* pod, QObject* parent) : QObject(parent), pod_(pod) {
    if (!pod_) return;
    // Qt signals are delivered on the pod's (GUI) thread; the atomics make the
    // values readable from the preview worker.
    connect(pod_, &EnginePod::stateChanged, this, [this](int state) {
        pod_state_.store(state);
        if (state == EnginePod::kCrashed) {
            std::lock_guard<std::mutex> lock(note_mutex_);
            last_error_ = pod_->last_error().toStdString();
        }
    });
    connect(pod_, &EnginePod::frameAvailable, this, [this](quint64 seq) {
        frames_.store(seq);
    });
    connect(pod_, &EnginePod::fpsChanged, this, [this](int fps) { fps_.store(fps); });
    connect(pod_, &EnginePod::bootFailed, this, [this](const QString& reason) {
        std::lock_guard<std::mutex> lock(note_mutex_);
        last_error_ = reason.toStdString();
    });
}

PodLiveEngine::~PodLiveEngine() {
    // Never stop the pod here: Ruby owns its lifecycle (the user may keep the
    // preview running while switching the studio between backends).
}

void PodLiveEngine::on_frame(quint64 seq) { frames_.store(seq); }

bool PodLiveEngine::attach(const caver::LiveEngineConfig& config, std::string* error) {
    if (!pod_) {
        if (error) *error = "no EnginePod instance";
        return false;
    }
    if (config.asset_instance_dir.empty()) {
        if (error) *error = "no asset instance directory";
        return false;
    }
    // Already booted on the same assets: reuse the running session, exactly as
    // the preview panel does when the user boots once and shifts scenes.
    if (pod_->is_alive() && attached_assets_ == config.asset_instance_dir) {
        attached_ = true;
        return true;
    }
    if (pod_->is_alive()) pod_->stop();

    config_ = config;
    const QString lib = config.engine_lib_path.empty()
                            ? EnginePod::default_engine_lib_path()
                            : QString::fromStdString(config.engine_lib_path);
    const QString bin = config.host_binary.empty()
                            ? EnginePod::find_swordfare_binary()
                            : QString::fromStdString(config.host_binary);
    if (!QFileInfo::exists(bin)) {
        if (error) *error = "swordfare binary not found: " + bin.toStdString();
        return false;
    }
    if (!QFileInfo::exists(lib)) {
        if (error) *error = "engine lib not found: " + lib.toStdString();
        return false;
    }

    bool started = false;
    // EnginePod::start touches QProcess, so it must run on the pod's thread.
    QMetaObject::invokeMethod(
        pod_,
        [&]() {
            started = pod_->start(QString::fromStdString(config.asset_instance_dir), config.width,
                                  config.height, lib, bin);
        },
        Qt::BlockingQueuedConnection);

    attached_ = started;
    attached_assets_ = config.asset_instance_dir;
    if (!started && error) *error = pod_->last_error().toStdString();
    std::lock_guard<std::mutex> lock(note_mutex_);
    notes_.clear();
    notes_.push_back("pod: " + std::string(started ? "booted" : "boot failed") + " (" +
                     std::to_string(config.width) + "x" + std::to_string(config.height) + ")");
    if (pod_->lua_console_port() != 0)
        notes_.push_back("guest Lua console on 127.0.0.1:" + std::to_string(pod_->lua_console_port()));
    return started;
}

void PodLiveEngine::detach() {
    attached_ = false;
    attached_assets_.clear();
    std::lock_guard<std::mutex> lock(note_mutex_);
    notes_.clear();
}

bool PodLiveEngine::load_scene(const std::string& scene_stem, std::string* error) {
    if (!pod_ || !pod_->is_alive()) {
        if (error) *error = "pod is not running";
        return false;
    }
    if (scene_stem.empty()) return true;
    const QString target = QString::fromStdString(scene_stem);
    // The engine's own Scene Shifter switches the loaded scene; going through it
    // keeps one load path (the pod's) instead of a second one here.
    QMetaObject::invokeMethod(pod_, [this, target]() { pod_->send_scene_shift(target); },
                              Qt::QueuedConnection);
    std::lock_guard<std::mutex> lock(note_mutex_);
    notes_.push_back("scene shift queued: " + scene_stem);
    return true;
}

void PodLiveEngine::step(double dt) {
    // The pod runs its own frame loop; the recovered preview's fixed step has no
    // meaning for it, so this is intentionally a no-op beyond bookkeeping.
    (void)dt;
}

void PodLiveEngine::set_paused(bool paused) {
    if (!pod_) return;
    QMetaObject::invokeMethod(pod_, [this, paused]() { pod_->send_pause(paused); },
                              Qt::QueuedConnection);
}

bool PodLiveEngine::read_state(std::vector<caver::ObjectRenderState>& out) {
    // Honest answer: the pod publishes frames, not transforms. Rather than invent
    // per-object state, report unavailability and point at the two real readback
    // routes (guest Lua console / SRE hooks).
    out.clear();
    std::lock_guard<std::mutex> lock(note_mutex_);
    const std::string note =
        "no per-object readback over the pod control channel (frames arrive as pixels; "
        "use the guest Lua console or SRE hooks for transforms)";
    for (const std::string& existing : notes_)
        if (existing == note) return false;
    notes_.push_back(note);
    return false;
}

caver::LiveEngineState PodLiveEngine::state() const {
    if (!pod_) return caver::LiveEngineState::Detached;
    switch (pod_state_.load()) {
        case EnginePod::kBooting: return caver::LiveEngineState::Booting;
        case EnginePod::kRunning: return caver::LiveEngineState::Running;
        case EnginePod::kPaused:  return caver::LiveEngineState::Paused;
        case EnginePod::kExited:  return caver::LiveEngineState::Stopped;
        case EnginePod::kCrashed: return caver::LiveEngineState::Failed;
        case EnginePod::kStopped:
        default:                  return attached_ ? caver::LiveEngineState::Stopped
                                                   : caver::LiveEngineState::Detached;
    }
}

std::string PodLiveEngine::status() const {
    std::string text = std::string(caver::to_string(state())) +
        " · frames=" + std::to_string(frames_.load()) +
        " fps=" + std::to_string(fps_.load());
    std::lock_guard<std::mutex> lock(note_mutex_);
    if (!last_error_.empty()) text += " · " + last_error_;
    return text;
}

bool PodLiveEngine::alive() const { return pod_ && pod_->is_alive(); }

std::vector<std::string> PodLiveEngine::notes() const {
    std::lock_guard<std::mutex> lock(note_mutex_);
    std::vector<std::string> notes = notes_;
    if (!last_error_.empty()) notes.push_back("error: " + last_error_);
    return notes;
}

} // namespace ruby::emulator
