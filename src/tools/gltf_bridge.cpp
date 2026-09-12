// gltf_bridge.cpp — symmetric POD <-> glTF/GLB bridge implementation.
//
// Both directions are thin adapters over the shipping export/import path
// (gltf_export_glb / gltf_import_glb). The shipping importer consumes a file
// path, so the in-memory helpers stage bytes through a unique temp file — this
// keeps the bridge honest (it runs exactly what `bin/ruby --glb2pod` and the
// viewer run) rather than duplicating a second, drifting serializer.

#include "gltf_bridge.h"
#include "pod_loader.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h> // getpid
#else
#include <process.h> // _getpid
#endif

namespace fs = std::filesystem;

namespace av {
namespace {

// A collision-resistant temp path (pid + monotonic counter). We can't return
// GLB bytes from the importer without a file because gltf_import_glb reads a
// path; staging through a temp file is the least-surprising bridge.
std::string unique_tmp_glb() {
    static std::atomic<uint64_t> ctr{0};
    const uint64_t n = ctr.fetch_add(1, std::memory_order_relaxed);
    fs::path p = fs::temp_directory_path() /
                 ("pod_bridge_" + std::to_string(
#if defined(_WIN32)
                      (unsigned long)_getpid()
#else
                      (unsigned long)::getpid()
#endif
                      ) + "_" + std::to_string(n) + ".glb");
    return p.string();
}

bool read_all(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

bool write_all(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    if (!data.empty()) f.write(reinterpret_cast<const char*>(data.data()),
                               static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

} // namespace

bool pod_to_glb(const PODModel& model,
                const std::vector<GLTFTextureImage>& images,
                std::vector<uint8_t>& out_glb,
                std::string* err,
                bool flip_v) {
    const std::string tmp = unique_tmp_glb();
    std::error_code ec;
    bool ok = gltf_export_glb(model, images, tmp, err, flip_v);
    if (ok) {
        ok = read_all(tmp, out_glb);
        if (!ok && err) *err = "pod_to_glb: wrote GLB but failed to read it back: " + tmp;
    }
    fs::remove(tmp, ec);
    return ok;
}

bool glb_to_pod(const std::vector<uint8_t>& glb,
                PODModel& out,
                std::vector<GLTFImageBuffer>& images,
                std::string* err,
                GLTFPBRInfo* pbr,
                float scale) {
    const std::string tmp = unique_tmp_glb();
    if (!write_all(tmp, glb)) {
        if (err) *err = "glb_to_pod: failed to stage GLB to temp: " + tmp;
        return false;
    }
    std::error_code ec;
    bool ok = gltf_import_glb(tmp, out, images, err, pbr, scale);
    fs::remove(tmp, ec);
    return ok;
}

bool pod_roundtrip_through_glb(const PODModel& in,
                               const std::vector<GLTFTextureImage>& images,
                               PODModel& out,
                               std::string* err) {
    std::vector<uint8_t> glb;
    if (!pod_to_glb(in, images, glb, err)) return false;
    std::vector<GLTFImageBuffer> imgs;
    GLTFPBRInfo pbr;
    return glb_to_pod(glb, out, imgs, err, &pbr, 1.0f);
}

} // namespace av
