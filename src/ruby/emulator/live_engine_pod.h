#pragma once
// live_engine_pod.h — caver::ILiveEngine implemented over the existing EnginePod.
//
// This is the only bridge between the recovered runtime (caver) and the genuine
// engine, and it adds no runner: EnginePod already spawns
// `swordfare --pod-preview`, loads libswordigo through the loader + Dynarmic,
// streams frames over the pod::FrameRing shm triple buffer, accepts control
// messages on the child's stdin (including the Scene Shifter) and exposes the
// guest's Raijin Lua console over TCP.
//
// The adapter therefore:
//   * attach()  → EnginePod::start (boots the pod if it is not already alive)
//   * load_scene() → EnginePod::send_scene_shift (the engine's own scene switch,
//     so no bespoke loading path is introduced)
//   * step()/set_paused() → EnginePod::send_pause
//   * read_state() → honest: the pod's control channel carries frames, not object
//     transforms, so it reports `false` today and says so in notes(). Per-object
//     readback will come from the guest Lua console (lua_console_port()) or from
//     SRE hooks, not from a second emulator.
//
// Control calls made from the preview worker thread are marshalled to the GUI
// thread (the pod's own thread) with Qt::QueuedConnection; state is cached in
// atomics so reads never touch Qt objects off-thread.

#include <QObject>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "ruby/caver/live_engine.h"

namespace ruby::emulator { class EnginePod; }

namespace ruby::emulator {

class PodLiveEngine : public QObject, public caver::ILiveEngine {
public:
    // `pod` must outlive the adapter (Ruby owns it).
    explicit PodLiveEngine(EnginePod* pod, QObject* parent = nullptr);
    ~PodLiveEngine() override;

    // The heartbeat callback: EnginePod pushes every new frame here so the
    // adapter can count frames and (once readback exists) keep a state cache.
    void on_frame(quint64 seq);

    // caver::ILiveEngine
    bool attach(const caver::LiveEngineConfig& config, std::string* error) override;
    void detach() override;
    bool load_scene(const std::string& scene_stem, std::string* error) override;
    void step(double dt) override;
    void set_paused(bool paused) override;
    bool read_state(std::vector<caver::ObjectRenderState>& out) override;
    caver::LiveEngineState state() const override;
    std::string status() const override;
    bool alive() const override;
    std::vector<std::string> notes() const override;

private:
    EnginePod* pod_ = nullptr;
    caver::LiveEngineConfig config_;
    bool attached_ = false;
    std::string attached_assets_;

    // Written from the GUI thread (Qt signals), read from the preview thread.
    std::atomic<int> pod_state_{0};
    std::atomic<uint64_t> frames_{0};
    std::atomic<int> fps_{0};
    mutable std::mutex note_mutex_;
    std::vector<std::string> notes_;
    std::string last_error_;
};

} // namespace ruby::emulator
