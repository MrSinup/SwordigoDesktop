// ============================================================================
// workspace_detect.cpp — see workspace_detect.h
// ============================================================================
#include "ruby/emulator/workspace_detect.h"

#include <filesystem>
#include <algorithm>
#include <set>

namespace fs = std::filesystem;

namespace ruby::emulator {

namespace {
constexpr const char* kMarkers[] = {
    "menu.scene/Resources",          // the very first scene the boot loads
    "menu.scene",                    // .scene unpacked variants
    "game_common_atlas_2x.atlas",    // shared UI/game atlas (always loaded)
    "ui_game_atlas_2x.atlas",
    "swordigo_title_2x.tex.png",
    "font_megalopolis_10_2x_2x.tex.png",
    "font_megalopolis_14_2x_2x.tex.png",
};
}

bool resources_looks_bootable(const std::string& resources_dir, int* score_out) {
    int score = 0;
    std::error_code ec;
    if (!fs::is_directory(resources_dir, ec)) {
        if (score_out) *score_out = 0;
        return false;
    }
    for (const char* m : kMarkers) {
        if (fs::exists(fs::path(resources_dir) / m, ec)) score++;
    }
    // A resources/ tree full of files but zero markers is still usually valid
    // for a workspace whose scenes live elsewhere — accept with score 1.
    if (score == 0) {
        int files = 0;
        for (auto& de : fs::directory_iterator(resources_dir, ec)) {
            if (++files > 8) break;   // non-empty tree
        }
        if (files > 0) score = 1;
    }
    if (score_out) *score_out = score;
    return score > 0;
}

static AssetCandidate make_candidate(const fs::path& instance) {
    AssetCandidate c;
    std::error_code ec;
    c.instance_dir = instance.string();
    c.resources_dir = (instance / "resources").string();
    c.name = instance.filename().string();
    resources_looks_bootable(c.resources_dir, &c.marker_score);
    return c;
}

std::vector<AssetCandidate> detect_asset_dirs(const std::string& root, bool allow_nested) {
    std::vector<AssetCandidate> out;
    std::set<std::string> seen;
    std::error_code ec;
    fs::path root_p(root);

    auto push_if_bootable = [&](const fs::path& instance) {
        if (!fs::is_directory(instance, ec)) return;
        int score = 0;
        if (!resources_looks_bootable((instance / "resources").string(), &score)) return;
        if (score == 0) return;
        AssetCandidate c = make_candidate(instance);
        std::string key = c.instance_dir;
        if (!seen.count(key)) {
            seen.insert(key);
            out.push_back(std::move(c));
        }
    };

    // 1) The root itself may BE the instance dir (workspace opened on assets).
    push_if_bootable(root_p);
    if (!allow_nested) return out;

    // 2) Classic layout: <root>/assets, <root>/assets13, <root>/assets2 … 
    std::vector<fs::path> dirs_to_scan;
    if (fs::is_directory(root_p, ec)) {
        for (auto& de : fs::directory_iterator(root_p, ec)) {
            if (!de.is_directory(ec)) continue;
            std::string name = de.path().filename().string();
            dirs_to_scan.push_back(de.path());
            // 3) One more level of nesting: <root>/<game>/assets*/…
            if (name.find("assets") == 0 || name.find("OpenSwordigo") == 0 ||
                name == "engine" || name == "projects" || name == "workspace" ||
                name == "mods") {
                for (auto& de2 : fs::directory_iterator(de.path(), ec)) {
                    if (de2.is_directory(ec)) dirs_to_scan.push_back(de2.path());
                }
            }
        }
    }
    for (const auto& d : dirs_to_scan) push_if_bootable(d);

    std::sort(out.begin(), out.end(),
              [](const AssetCandidate& a, const AssetCandidate& b) {
                  return a.marker_score > b.marker_score;
              });
    return out;
}

std::vector<AssetCandidate> detect_from_roots(const std::vector<std::string>& roots) {
    std::vector<AssetCandidate> out;
    std::set<std::string> seen;
    for (const auto& r : roots) {
        for (auto& c : detect_asset_dirs(r)) {
            if (seen.insert(c.instance_dir).second) out.push_back(std::move(c));
        }
    }
    std::sort(out.begin(), out.end(),
              [](const AssetCandidate& a, const AssetCandidate& b) {
                  return a.marker_score > b.marker_score;
              });
    return out;
}

bool looks_like_instance_dir(const std::string& path) {
    return resources_looks_bootable((fs::path(path) / "resources").string());
}

} // namespace ruby::emulator
