#pragma once
// live_engine.h — the seam between caver's preview engine and the *real* engine.
//
// We do NOT run libswordigo ourselves. Ruby already boots the genuine engine:
// `ruby/emulator/engine_pod.{h,cpp}` spawns `swordfare --pod-preview`, which
// loads libswordigo through the loader + Dynarmic and streams finished frames
// over the `pod::FrameRing` shared-memory triple buffer, with pause / mute /
// input / scene-shift control messages on the child's stdin and the guest's Lua
// console exposed over TCP (platform/pod_ipc.h). That is the reference path and
// it stays the only one.
//
// caver's job is the *recovered* runtime: fast, headless, per-component
// inspectable, and the thing the SCL studio / diorama / lint drive. The two are
// complementary, and the only thing they need to share is this interface —
// caver asks a live engine to load a scene and step it, and (optionally) to read
// back what it observed. The adapter that implements this over EnginePod lives
// beside EnginePod (`ruby/emulator/live_engine_pod.{h,cpp}`), so caver stays
// Qt-free and no second emulator runner exists anywhere in the tree.

#include <string>
#include <vector>

#include "ruby/caver/runtime.h"

namespace caver {

// What a live engine reports about itself.
enum class LiveEngineState {
    Detached = 0,
    Booting,
    Running,
    Paused,
    Stopped,
    Failed,
};

struct LiveEngineConfig {
    std::string asset_instance_dir;   // folder containing resources/
    std::string engine_lib_path;      // engine/<ver>/<abi>/libswordigo.so
    std::string host_binary;          // bin/swordfare
    std::string scene;                // scene stem to load ("town_part1")
    int  width = 1280;
    int  height = 720;
};

// A live engine driven by the host application. Implementations must be safe to
// call from the preview thread (the EnginePod-backed adapter marshals to the GUI
// thread internally); `read_state` may legitimately return false when the engine
// exposes no per-object readback yet.
class ILiveEngine {
public:
    virtual ~ILiveEngine() = default;

    // Bring the engine up (idempotent). `error` receives the reason on failure.
    virtual bool attach(const LiveEngineConfig& config, std::string* error) = 0;
    virtual void detach() = 0;

    // Load/switch the scene the engine should be running.
    virtual bool load_scene(const std::string& scene_stem, std::string* error) = 0;

    // Advance engine time (the adapter is free to treat this as a no-op when the
    // engine already runs on its own clock).
    virtual void step(double dt) = 0;

    virtual void set_paused(bool paused) = 0;

    // Read per-object state back out of the running engine, in caver's shape, so
    // the studio can diff recovered-vs-real. Returns false when unavailable.
    virtual bool read_state(std::vector<ObjectRenderState>& out) = 0;

    virtual LiveEngineState state() const = 0;
    virtual std::string status() const = 0;
    virtual bool alive() const = 0;
    // Human-readable notes for the preview panel ("paused by user",
    // "no transform readback over the pod control channel yet").
    virtual std::vector<std::string> notes() const { return {}; }
};

const char* to_string(LiveEngineState state);

} // namespace caver
