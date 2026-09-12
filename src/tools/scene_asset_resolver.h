#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Shared, UI-free scene asset discovery.  Ruby's renderer historically kept
// this logic inside asset_viewer.cpp; Qt, CLI and ImGui can now share it.
namespace av::assets {
std::filesystem::path resolve_pod(const std::filesystem::path& scene_path,
                                  const std::string& mesh_name,
                                  const std::vector<std::filesystem::path>& extra_roots);
std::filesystem::path resolve_pod(const std::filesystem::path& scene_path,
                                  const std::string& mesh_name,
                                  const std::filesystem::path& assets_root = {});
std::vector<std::filesystem::path> texture_candidates(const std::filesystem::path& model_path,
                                                       const std::string& texture_name,
                                                       const std::vector<std::filesystem::path>& extra_roots = {});
}
