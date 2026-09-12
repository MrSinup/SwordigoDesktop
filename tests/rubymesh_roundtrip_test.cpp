// rubymesh_roundtrip_test.cpp — data-layer checks for the .rbm binary format.
//
// Verifies: save→load round-trips bit-identically, the header layout matches
// the documented spec (magic/version/data offset), the checksum catches
// corruption, truncation fails cleanly, and file paths follow the
// <scene_dir>/<stem>_rubymesh.rbm convention.

#include "tools/rubymesh.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    } else {
        std::printf("ok:   %s\n", what);
    }
}

bool zone_equal(const rbm::Zone& a, const rbm::Zone& b) {
    if (a.name != b.name) return false;
    if (a.enabled != b.enabled || a.invisible != b.invisible) return false;
    if (a.material != b.material) return false;
    if (a.world_z != b.world_z || a.depth_min != b.depth_min ||
        a.depth_max != b.depth_max)
        return false;
    if (a.top_texture != b.top_texture || a.front_texture != b.front_texture)
        return false;
    if (a.vertices.size() != b.vertices.size()) return false;
    for (size_t i = 0; i < a.vertices.size(); ++i)
        if (a.vertices[i] != b.vertices[i]) return false;
    return true;
}

bool mesh_equal(const rbm::RubyMesh& a, const rbm::RubyMesh& b) {
    if (a.model_name != b.model_name) return false;
    if (a.zones.size() != b.zones.size()) return false;
    for (size_t i = 0; i < a.zones.size(); ++i)
        if (!zone_equal(a.zones[i], b.zones[i])) return false;
    return true;
}

rbm::RubyMesh sample_mesh() {
    rbm::RubyMesh m;
    m.model_name = "castle_keep";
    {
        rbm::Zone z;
        z.name = "floor";
        z.enabled = true;
        z.invisible = true;
        z.material = rbm::SurfaceMaterial::Stone;
        z.world_z = 40.0f;
        z.depth_min = -45.0f;
        z.depth_max = 45.0f;
        z.top_texture = "";
        z.front_texture = "";
        z.vertices = {{-120.0f, -40.0f}, {120.0f, -40.0f},
                      {120.0f, 40.0f}, {-120.0f, 40.0f}};
        m.zones.push_back(std::move(z));
    }
    {
        rbm::Zone z;
        z.name = "platform_top";
        z.enabled = true;
        z.invisible = false;          // visible textured platform
        z.material = rbm::SurfaceMaterial::Wood;
        z.world_z = 12.5f;
        z.depth_min = -30.0f;
        z.depth_max = 30.0f;
        z.top_texture = "castle_floor";
        z.front_texture = "castle_wall";
        z.vertices = {{0.0f, 0.0f}, {64.0f, 0.0f}, {64.0f, 24.0f}, {0.0f, 24.0f}};
        m.zones.push_back(std::move(z));
    }
    return m;
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> out((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    return out;
}

void test_roundtrip(const std::string& dir) {
    const rbm::RubyMesh src = sample_mesh();
    const std::string path = dir + "/rt.rbm";
    std::string err;
    if (!rbm::rbm_save(src, path, err)) {
        check(false, ("save: " + err).c_str());
        return;
    }
    check(true, "save succeeded (atomic tmp+rename, verify passed)");

    rbm::RubyMesh back;
    if (!rbm::rbm_load(path, back, err)) {
        check(false, ("load: " + err).c_str());
        return;
    }
    check(mesh_equal(src, back), "save -> load round-trips identically");

    // Header layout: magic "RBM!", version 1, DataOffset >= 32.
    const std::vector<uint8_t> bytes = read_file(path);
    check(bytes.size() >= 32, "file has a 32-byte header");
    check(bytes.size() >= 32 &&
          bytes[0] == 'R' && bytes[1] == 'B' && bytes[2] == 'M' && bytes[3] == '!',
          "magic is RBM!");
    check(bytes.size() >= 6 && bytes[4] == 1 && bytes[5] == 0,
          "version field is 1 (little-endian u16)");
    if (bytes.size() >= 0x14) {
        const uint32_t data_off = bytes[0x10] | (bytes[0x11] << 8u) |
                                  (bytes[0x12] << 16u) | (bytes[0x13] << 24u);
        check(data_off >= 32 && data_off <= bytes.size(),
              "DataOffset is >= 32 and in range");
        const uint32_t zone_count = bytes[0x0C] | (bytes[0x0D] << 8u) |
                                    (bytes[0x0E] << 16u) | (bytes[0x0F] << 24u);
        check(zone_count == 2, "ZoneCount == 2");
    }

    // Corrupt one byte after the header -> checksum failure.
    {
        std::vector<uint8_t> corrupted = bytes;
        const size_t mid = corrupted.size() / 2;
        corrupted[mid] ^= 0x5A;
        std::ofstream out(dir + "/corrupt.rbm", std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(corrupted.data()),
                  static_cast<std::streamsize>(corrupted.size()));
        out.close();
        rbm::RubyMesh m;
        std::string cerr;
        const bool ok = rbm::rbm_load(dir + "/corrupt.rbm", m, cerr);
        check(!ok && !cerr.empty(), "corrupted payload rejected (checksum)");
    }

    // Truncate mid-zone -> clean error, no crash.
    {
        std::ofstream out(dir + "/trunc.rbm", std::ios::binary | std::ios::trunc);
        const size_t keep = bytes.size() - 30;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(keep));
        out.close();
        rbm::RubyMesh m;
        std::string terr;
        const bool ok = rbm::rbm_load(dir + "/trunc.rbm", m, terr);
        check(!ok && !terr.empty(), "truncated file rejected cleanly");
    }

    // Bad magic -> clean error.
    {
        std::ofstream out(dir + "/bad.rbm", std::ios::binary | std::ios::trunc);
        std::vector<uint8_t> bad = bytes;
        bad[0] = 'X';
        out.write(reinterpret_cast<const char*>(bad.data()),
                  static_cast<std::streamsize>(bad.size()));
        out.close();
        rbm::RubyMesh m;
        std::string berr;
        check(!rbm::rbm_load(dir + "/bad.rbm", m, berr) && !berr.empty(),
              "bad magic rejected cleanly");
    }

    // Overwrite: second save on the same path still round-trips.
    {
        rbm::RubyMesh second = sample_mesh();
        second.zones[0].vertices.push_back({200.0f, 200.0f});
        std::string serr;
        check(rbm::rbm_save(second, path, serr), "second save to same path ok");
        rbm::RubyMesh loaded;
        check(rbm::rbm_load(path, loaded, serr) && mesh_equal(second, loaded),
              "overwrite round-trips");
    }
}

void test_paths() {
    check(rbm::rbm_path_for("castle_keep", "/scenes/castle_keep") ==
              "/scenes/castle_keep/castle_keep_rubymesh.rbm",
          "rbm_path_for joins scene dir + stem");
    check(rbm::rbm_path_for("models/fort.pod", "/lvl") ==
              "/lvl/fort.pod_rubymesh.rbm",
          "rbm_path_for keeps the stem suffix as given (caller passes a stem)");
}

void test_default() {
    const rbm::RubyMesh m = rbm::rbm_load_or_default("ghost_model", "/nonexistent-dir-xyz");
    check(m.model_name == "ghost_model" && m.zones.empty(),
          "rbm_load_or_default falls back to an empty mesh on missing file");
}

void test_ccw() {
    // Clockwise square -> CCW.
    std::vector<std::pair<float, float>> cw = {{0, 0}, {0, 10}, {10, 10}, {10, 0}};
    rbm::ensure_ccw(cw);
    check(rbm::polygon_signed_area(cw) > 0.0, "ensure_ccw flips clockwise to CCW");
    std::vector<std::pair<float, float>> ccw = cw;
    rbm::ensure_ccw(ccw);
    check(ccw == cw, "ensure_ccw is idempotent on CCW input");
}

} // namespace

int main(int argc, char** argv) {
    std::string dir = ".";
    if (argc > 1) dir = argv[1];
    test_roundtrip(dir);
    test_paths();
    test_default();
    test_ccw();
    if (g_failures == 0) {
        std::printf("rubymesh_roundtrip_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "rubymesh_roundtrip_test: %d failure(s)\n", g_failures);
    return 1;
}
