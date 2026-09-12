// ============================================================================
// workspace_detect.h — Ruby GG asset-workspace auto-detection
//
// The engine pod needs a *Swordigo asset directory*: a folder that contains a
// `resources/` tree the 1.4.13 engine boots from (menu.scene + common atlases,
// etc.). Ruby GG editors usually point at the same tree one level up, so this
// module scans a candidate root (project folder, home, ...) for:
//     <root>/resources/…                    (workspace == instance dir)
//     <root>/assets*/resources/…            (classic / modded instances)
//     <root>/<name>/resources/…             (arbitrary instance subfolders)
// and validates each find with cheap boot markers so "Run Game" boots first
// try instead of showing the engine's "cannot find menu.scene" path.
// ============================================================================
#pragma once

#include <string>
#include <vector>

namespace ruby::emulator {

struct AssetCandidate {
    std::string instance_dir;    // folder passed to `swordfare --assets`
    std::string resources_dir;   // instance_dir + "/resources"
    std::string name;            // display label (folder name)
    int marker_score = 0;        // boot-marker evidence (higher = better)
    bool operator<(const AssetCandidate& o) const { return marker_score < o.marker_score; }
};

// Heuristic boot markers looked for inside <instance>/resources/.
// A candidate is accepted when any marker exists; score grows with more.
bool resources_looks_bootable(const std::string& resources_dir, int* score_out = nullptr);

// Scan `root` (and, when allow_nested, immediate subfolders named assets* or
// holding resources/ directly) for bootable asset directories.
std::vector<AssetCandidate> detect_asset_dirs(const std::string& root,
                                              bool allow_nested = true);

// Convenience for a dialog: given several roots, return every unique candidate.
std::vector<AssetCandidate> detect_from_roots(const std::vector<std::string>& roots);

// True when `path` looks like an instance dir (contains bootable resources/).
bool looks_like_instance_dir(const std::string& path);

} // namespace ruby::emulator
