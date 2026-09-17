#pragma once

#include <QString>
#include <QStringList>
#include <vector>

namespace build_studio {

enum class TargetState {
    UpToDate, // Binary exists and source untouched
    Modified, // Source modified since binary was built
    Missing   // Binary does not exist yet
};

struct TargetInfo {
    QString name;
    QString displayName;
    QString description;
    QString category; // "Applications", "Engines & Guest", "Libraries"
    QString artifactPath; // e.g. "bin/swordfare"
    QStringList sourcePatterns; // paths/globs that trigger update
    bool defaultSelected;
};

inline std::vector<TargetInfo> getAllProjectTargets() {
    return {
        // Applications
        { "swordfare", "Swordfare (Main Game)", "Primary desktop game launcher", "Applications",
          "bin/swordfare", { "src/platform/", "src/launcher/", "src/game/", "src/loader/", "src/main.cpp" }, true },

        { "ruby_gg", "Ruby GG (Studio)", "Swordigo Studio Qt6 editor", "Applications",
          "bin/ruby_gg", { "src/ruby/" }, false },

        { "ruby", "Ruby SDK", "Asset viewer & scene editor tool", "Applications",
          "bin/ruby", { "src/tools/rubymesh" }, false },

        { "ruby_cli", "Ruby CLI", "Headless FileRift CLI utility", "Applications",
          "bin/ruby_cli", { "src/tools/ruby_cli.cpp" }, false },

        { "opensw", "OpenSwordigo", "Recovered native engine boot runner", "Applications",
          "bin/opensw", { "src/opensw" }, false },

        { "scl_graph_viewer", "SCL Graph Viewer", "Interactive visual graph viewer", "Applications",
          "bin/scl_graph_viewer", { "src/tools/scl_graph_viewer.cpp" }, false },

        // Engines & Guest Runtimes
        { "dynarmic-build", "Dynarmic JIT", "ARM64 Dynamic Recompiler core", "Engines & Guest",
          "deps/dynarmic/build/src/dynarmic/libdynarmic.a", { "deps/dynarmic/src/" }, false },

        { "sre", "libsre12 (1.4.12)", "ARM64 guest hooks for 1.4.12", "Engines & Guest",
          "bin/libs/libsre12.so", { "src/sre/sre12/", "src/sre/base/" }, false },

        { "sre13", "libsre13 (1.4.13)", "ARM64 guest hooks for 1.4.13", "Engines & Guest",
          "bin/libs/libsre13.so", { "src/sre/sre13/", "src/sre/base/" }, false },

        // Core Libraries
        { "caver", "Caver Runtime", "Recovered engine runtime library", "Libraries",
          "", { "src/caver/" }, false },

        { "filerift", "FileRift", "Binary schema, parser & protobuf engine", "Libraries",
          "", { "src/tools/filerift" }, false },

        { "swcore", "swcore", "Fundamental core utility library", "Libraries",
          "", { "src/core/" }, false },

        { "swpod", "swpod", "PowerVR POD loader & mesh converter", "Libraries",
          "", { "src/tools/pod" }, false },

        { "swgui", "swgui", "Desktop GUI & overlay primitives", "Libraries",
          "", { "src/platform/swordfare_gui.cpp" }, false },

        { "swgfx", "swgfx", "Graphics abstraction & shaders", "Libraries",
          "", { "src/platform/gfx" }, false },

        { "swemu", "swemu", "Emulator CPU execution core", "Libraries",
          "", { "src/platform/emulator" }, false }
    };
}

enum class TargetPreset {
    Smart,      // Only targets that are Missing or Modified
    FullGame,   // swordfare + dynarmic-build + sre
    StudioOnly, // ruby_gg
    AllTargets,
    Custom
};

inline QStringList getPresetTargetNames(TargetPreset preset) {
    switch (preset) {
        case TargetPreset::FullGame:
            return { "swordfare", "sre" };
        case TargetPreset::StudioOnly:
            return { "ruby_gg" };
        case TargetPreset::AllTargets: {
            QStringList all;
            for (const auto& t : getAllProjectTargets()) {
                all.append(t.name);
            }
            return all;
        }
        default:
            return { "swordfare", "sre" };
    }
}

} // namespace build_studio
