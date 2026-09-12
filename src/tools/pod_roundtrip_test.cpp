// pod_roundtrip_test.cpp — standalone POD load→write→reload structural verifier.
//
// Phase-1 acceptance harness (pod_master/05). For every .POD passed on the
// command line (or found under a directory arg) it:
//   1. reads the file bytes
//   2. av::pod_parse() -> model A
//   3. av::pod_write() to a temp file
//   4. av::pod_parse() the temp -> model B
//   5. compares the structural fingerprints of A and B
//
// A mismatch means the loader and writer disagree — i.e. the round-trip is not
// stable. Exit code 0 iff every file round-trips cleanly.
//
// Built OUTSIDE cmake by directly including the two standalone TUs (both are
// pure namespace av, no OpenGL/ImGui), e.g.:
//   g++ -std=c++17 -O1 -I src/tools src/tools/pod_roundtrip_test.cpp -o /tmp/pod_rt
//
// (pod_loader.cpp / pod_writer.cpp are self-contained per their headers.)

#include "pod_loader.h"
#include "pod_writer.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
    return d;
}

// A compact structural fingerprint: the parts the loader/writer must preserve.
static std::string fingerprint(const av::PODModel& m) {
    std::ostringstream o;
    o << "meshes=" << m.meshes.size()
      << " nodes=" << m.nodes.size()
      << " mats=" << m.materials.size()
      << " texs=" << m.texture_filenames.size()
      << " frames=" << m.num_frames
      << " fps=" << m.fps << "\n";
    for (size_t i = 0; i < m.meshes.size(); ++i) {
        const auto& me = m.meshes[i];
        o << "  mesh[" << i << "] v=" << me.num_vertices
          << " f=" << me.num_faces
          << " idx=" << me.indices.size()
          << " pos=" << me.positions.size()
          << " nrm=" << me.normals.size()
          << " uv=" << me.uvs.size()
          << " bpv=" << me.bones_per_vertex
          << " bi=" << me.bone_indices.size()
          << " bw=" << me.bone_weights.size()
          << " mtype=" << me.mesh_type << "\n";
    }
    return o.str();
}

int main(int argc, char** argv) {
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        std::error_code ec;
        if (fs::is_directory(a, ec)) {
            for (auto& e : fs::recursive_directory_iterator(a, ec)) {
                if (!e.is_regular_file()) continue;
                auto p = e.path().string();
                auto ext = e.path().extension().string();
                for (auto& c : ext) c = (char)tolower(c);
                if (ext == ".pod") files.push_back(p);
            }
        } else {
            files.push_back(a);
        }
    }
    if (files.empty()) {
        std::fprintf(stderr, "usage: pod_rt <file.pod|dir> ...\n");
        return 2;
    }

    const std::string tmp = (fs::temp_directory_path() / "pod_rt_tmp.POD").string();
    int ok = 0, fail = 0, unreadable = 0;
    for (const auto& path : files) {
        auto bytes = read_file(path);
        if (bytes.empty()) { ++unreadable; continue; }

        av::PODModel a = av::pod_parse(bytes.data(), bytes.size());

        std::string werr;
        if (!av::pod_write(a, tmp, &werr)) {
            std::fprintf(stderr, "[WRITE-FAIL] %s : %s\n", path.c_str(), werr.c_str());
            ++fail; continue;
        }
        auto rb = read_file(tmp);
        av::PODModel b = av::pod_parse(rb.data(), rb.size());

        std::string fa = fingerprint(a), fb = fingerprint(b);
        if (fa == fb) {
            ++ok;
        } else {
            ++fail;
            std::fprintf(stderr, "[MISMATCH] %s\n--- A ---\n%s--- B ---\n%s\n",
                         path.c_str(), fa.c_str(), fb.c_str());
        }
    }

    std::printf("\n=== POD round-trip: %d ok, %d mismatched, %d unreadable / %zu total ===\n",
                ok, fail, unreadable, files.size());
    return fail == 0 ? 0 : 1;
}
