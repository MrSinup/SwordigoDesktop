// pod_game_reader_contract_test.cpp — our writer must emit the tags the GAME
// reads, not just the ones our own loader reads. Those two sets are not equal,
// and the difference is invisible from inside this repo: ruby_gg rendered every
// file happily while the game collapsed them into a "spirit sheet".
//
// The authority is the decompiled reader in the shipped binary
// (OpenSwordigo/arm64_13/functions/global/0000000000580AB8__sub_580AB8.c, the
// node section at case 0x7DD):
//
//   5004 Position / 5005 Rotation / 5006 Scale
//        static TRS; the node-close handler (0x800007DD) turns them into
//        1-frame pfAnimPosition/Rotation/Scale arrays when the corresponding
//        animation tag was absent.
//   5007 / 5008 / 5009
//        pfAnimPosition / pfAnimRotation / pfAnimScale (3 / 4 / 7 floats per
//        frame). This is what stock PVRGeoPOD writes for the static pose too,
//        verified against PVRShaman/Example/POD/OGL/*.pod.
//   5010
//        `case 0x1392u: goto LABEL_202;` — the 64-byte payload is SKIPPED and
//        nothing is stored. THE TAG IS A NO-OP. A node whose only transform is
//        5010 ends up with pfAnimPosition == NULL, so
//        CPVRTModelPOD::GetTranslation returns without writing its output and
//        the node's world matrix is assembled from uninitialised storage.
//   5011
//        pfAnimMatrix, stored at node+88 and used verbatim as the node's local
//        matrix by GetWorldMatrixNoCache when nAnimFlags bit 3 is clear. This
//        is the correct home for a raw 4x4.
//
// So this test scans the bytes our own writer produced — with a tag walker that
// shares no code with pod_loader — and asserts:
//
//   1. Tag 5010 never appears in any file we write.
//   2. Every node carries a transform the game will actually read
//      (5004/5005/5006, 5007/5008/5009 or a 64-byte 5011).
//   3. A raw matrix survives verbatim as a 64-byte 5011 payload.
//   4. The mesh section keeps its stock shape: 6005 == 0 and a 64-byte 6020.
//   5. The index stream length agrees with the declared face count, because the
//      reader sizes its index memcpy from PVRTModelPODCountIndices().
//
// It then re-parses through av::pod_parse to prove our own loader reads the new
// tag the same way, so the fix does not trade one reader for the other.
//
// Real converted assets under the user's asset root are scanned too, which is
// what actually caught the bug in situ; the scan SKIPs (loudly) when absent.

#include "tools/pod_loader.h"
#include "tools/pod_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int checks = 0;
int failures = 0;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    ++checks;
    if (ok) {
        std::printf("  ok   %s%s%s\n", what.c_str(),
                    detail.empty() ? "" : "  —  ", detail.c_str());
    } else {
        ++failures;
        std::printf("  FAIL %s%s%s\n", what.c_str(),
                    detail.empty() ? "" : "  —  ", detail.c_str());
    }
}

// ─── A POD tag walker, deliberately independent of pod_loader ──────────
//
// The file is a linear stream of (tag, length) pairs; the top bit of the tag
// means "this tag closes the current section". Walking it flat is exactly what
// the game's reader does, and it never desynchronises.
struct TagHit {
    uint32_t tag = 0;
    uint32_t len = 0;
    size_t   payload = 0; // byte offset of the payload
};

struct Parsed {
    std::vector<TagHit> tags;
    const uint8_t*      data = nullptr;
    size_t              size = 0;
};

bool walk(const std::vector<uint8_t>& buf, Parsed& out) {
    out.data = buf.data();
    out.size = buf.size();
    size_t pos = 0;
    while (pos + 8 <= buf.size()) {
        uint32_t tag = 0, len = 0;
        std::memcpy(&tag, buf.data() + pos, 4);
        std::memcpy(&len, buf.data() + pos + 4, 4);
        pos += 8;
        const uint32_t base = tag & 0x7FFFFFFFu;
        if ((tag & 0x80000000u) != 0) continue; // section close
        out.tags.push_back({base, len, pos});
        if (len > buf.size() - pos) return false; // truncated
        pos += len;
    }
    return true;
}

// ─── The stock grammar, enforced ──────────────────────────────────────
//
// `walk` above is deliberately lenient: it models libswordigo's reader, which
// is length-driven and happily skips a block that has no close marker.  But
// the *writer* contract is stricter, and it is not an opinion — it is verbatim
// what the shipped tool does.  PVRShamanGUI's CPVRTModelPOD::SavePOD
// (0x753370) brackets every block with:
//
//   sub_74BA40(f, tag, len):  fwrite(<tag:u16><0:u16>); fwrite(<len:u32>)
//   sub_74BAB0(f, tag):       fwrite(<tag:u16><0x8000:u16>); fwrite(<0:u32>)
//
// and sub_74BB90 writes nothing at all when its source pointer is NULL.  See
// OpenSwordigo/PVRToolsDecomp/POD_WRITER_GRAMMAR.md.  A file that omits the
// close pair still loads in this tree (our loader is lenient too, which is why
// the bug hid for so long) but it is not a stock POD.
//
// So: <tag> <len> <payload> <tag|0x80000000> <0>, recursively, everywhere.

const std::set<uint32_t> kContainers = {
    1001, 2012, 2013, 2014, 2015, 6003, 6006, 6007, 6008, 6009, 6010, 6011,
    6012, 6013 };

struct GrammarReport {
    size_t      blocks = 0;
    size_t      leaves = 0;
    size_t      missing_close = 0;
    size_t      bad_close_tag = 0;
    size_t      nonzero_close = 0;
    std::string first_problem;
};

void grammar_note(GrammarReport& r, const std::string& what, uint32_t tag) {
    if (r.first_problem.empty()) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s (tag %u)", what.c_str(), tag);
        r.first_problem = buf;
    }
}

// Walks [pos, ...) as a sequence of siblings, consuming each block's close pair.
// Returns the offset after the last block, or SIZE_MAX on a hard error.
size_t grammar_scan(const std::vector<uint8_t>& b, size_t pos, GrammarReport& r) {
    while (pos + 8 <= b.size()) {
        uint32_t tag = 0, len = 0;
        std::memcpy(&tag, b.data() + pos, 4);
        std::memcpy(&len, b.data() + pos + 4, 4);
        if ((tag & 0x80000000u) != 0) return pos; // a close we were not awaiting
        pos += 8;
        ++r.blocks;
        if (len > b.size() - pos) return SIZE_MAX;
        if (kContainers.count(tag) != 0) {
            const size_t after = grammar_scan(b, pos, r);
            if (after == SIZE_MAX) return SIZE_MAX;
            if (after == pos) {
                ++r.missing_close;
                grammar_note(r, "container never closed", tag);
                return SIZE_MAX;
            }
            pos = after;
        } else {
            ++r.leaves;
            pos += len;
        }
        // Every block, leaf or container, must now be closed by its own tag.
        if (pos + 8 > b.size()) {
            ++r.missing_close;
            grammar_note(r, "block runs to EOF with no close", tag);
            return SIZE_MAX;
        }
        uint32_t close_tag = 0, close_zero = 0;
        std::memcpy(&close_tag, b.data() + pos, 4);
        std::memcpy(&close_zero, b.data() + pos + 4, 4);
        if ((close_tag & 0x80000000u) == 0) {
            ++r.missing_close;
            grammar_note(r, "next block is not a close", tag);
            return SIZE_MAX;
        }
        if ((close_tag & 0x7FFFFFFFu) != tag) {
            ++r.bad_close_tag;
            grammar_note(r, "close tag does not match its block", tag);
        }
        if (close_zero != 0) {
            ++r.nonzero_close;
            grammar_note(r, "close pair's second dword is not zero", tag);
        }
        pos += 8;
    }
    return pos;
}

int count_tag(const Parsed& p, uint32_t tag) {
    int n = 0;
    for (const TagHit& t : p.tags) if (t.tag == tag) ++n;
    return n;
}

std::vector<const TagHit*> all_of(const Parsed& p, uint32_t tag) {
    std::vector<const TagHit*> out;
    for (const TagHit& t : p.tags) if (t.tag == tag) out.push_back(&t);
    return out;
}

// Node and mesh blocks are delimited by their 2013 / 2012 container tags;
// everything between one and the next belongs to that record.
struct Span {
    std::vector<const TagHit*> tags;
    const TagHit* find(uint32_t tag) const {
        for (const TagHit* t : tags) if (t->tag == tag) return t;
        return nullptr;
    }
    size_t count(uint32_t tag) const {
        size_t n = 0;
        for (const TagHit* t : tags) if (t->tag == tag) ++n;
        return n;
    }
};

std::vector<Span> spans_of(const Parsed& p, uint32_t open_tag) {
    std::vector<Span> spans;
    bool open = false;
    for (const TagHit& t : p.tags) {
        if (t.tag == open_tag) { spans.push_back(Span{}); open = true; continue; }
        if (open) spans.back().tags.push_back(&t);
    }
    return spans;
}

uint32_t u32_at(const Parsed& p, const TagHit* t) {
    uint32_t v = 0;
    if (t && t->len >= 4) std::memcpy(&v, p.data + t->payload, 4);
    return v;
}

// ─── Fixtures ─────────────────────────────────────────────────────────

// Exactly the shape that broke in-game: a Sketchfab GLB whose nodes carry a
// `matrix` instead of TRS. Node 1 of mh-60l_dap_usa.glb, verbatim.
const float kSketchfabMatrix[16] = {
    0.009999999776482582f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.009999999776482582f, 0.0f,
    0.0f, -0.009999999776482582f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
};

av::PODModel make_matrix_node_model() {
    av::PODModel m;
    m.version = "AB.POD.2.0";
    m.num_mesh_nodes = 1;
    m.num_frames = 0;
    m.fps = 30.0f;

    av::PODMesh mesh;
    mesh.num_vertices = 3;
    mesh.num_faces = 1;
    mesh.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    mesh.normals   = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.uvs       = {0, 0, 1, 0, 0, 1};
    mesh.indices   = {0, 1, 2};
    m.meshes.push_back(mesh);

    av::PODMaterial mat;
    mat.name = "default";
    m.materials.push_back(mat);
    m.texture_filenames.push_back("tex.pvr");

    // The mesh node: transform only through a raw matrix.
    av::PODNode n;
    n.name = "mesh_root";
    n.object_index = 0;
    n.material_index = 0;
    n.parent_index = -1;
    n.has_matrix = true;
    std::copy(kSketchfabMatrix, kSketchfabMatrix + 16, n.matrix);
    m.nodes.push_back(n);
    return m;
}

// A minimal skinned model: node 0 is the mesh, nodes 1/2 are the bones.  The
// skeleton marking rule (name starts with "Bone", plus ancestors) therefore
// yields the two-slot table {1, 2}, and the vertices reference slots 0 and 1.
av::PODModel make_skinned_model(bool supply_batch_table) {
    av::PODModel m;
    m.version = "AB.POD.2.0";
    m.num_mesh_nodes = 1;

    av::PODMesh mesh;
    mesh.num_vertices = 3;
    mesh.num_faces = 1;
    mesh.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    mesh.normals   = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.uvs       = {0, 0, 1, 0, 0, 1};
    mesh.indices   = {0, 1, 2};
    mesh.bones_per_vertex = 1;
    mesh.bone_indices = {0, 1, 1};
    mesh.bone_weights = {1, 1, 1};
    if (supply_batch_table) {
        mesh.has_bone_batches = true;
        mesh.bone_batches.indices = {1, 2};   // POD node index per slot
        mesh.bone_batches.counts  = {2};
        mesh.bone_batches.offsets = {0};
        mesh.bone_batches.count   = 1;
        mesh.bone_batches.max_bones = 2;
    }
    m.meshes.push_back(mesh);

    av::PODMaterial mat;
    mat.name = "default";
    m.materials.push_back(mat);
    m.texture_filenames.push_back("tex.pvr");

    av::PODNode root;
    root.name = "mesh_root";
    root.object_index = 0;
    root.material_index = 0;
    root.parent_index = -1;
    root.has_translation = root.has_rotation = root.has_scale = true;
    m.nodes.push_back(root);

    av::PODNode hip;
    hip.name = "Bone_Hips";
    hip.object_index = -1;
    hip.parent_index = 0;
    hip.has_translation = hip.has_rotation = hip.has_scale = true;
    m.nodes.push_back(hip);

    av::PODNode foot;
    foot.name = "Bone_Foot";
    foot.object_index = -1;
    foot.parent_index = 1;
    foot.has_translation = foot.has_rotation = foot.has_scale = true;
    m.nodes.push_back(foot);
    return m;
}

} // namespace
namespace {
// The table must name the skeleton's nodes in slot order, whoever built it.
void check_bone_batch_slots(const fs::path& path,
                            const std::vector<uint32_t>& expect) {
    std::vector<uint8_t> buf;
    if (FILE* f = std::fopen(path.string().c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n > 0) {
            buf.resize((size_t)n);
            if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) buf.clear();
        }
        std::fclose(f);
    }
    Parsed p;
    if (buf.empty() || !walk(buf, p)) {
        check(false, "skinned fixture is readable", path.string());
        return;
    }
    const std::vector<Span> meshes = spans_of(p, 2012);
    const TagHit* b15 = meshes.empty() ? nullptr : meshes[0].find(6015);
    bool ok = b15 && b15->len == expect.size() * 4;
    if (ok) {
        for (size_t i = 0; i < expect.size(); ++i) {
            uint32_t v = 0;
            std::memcpy(&v, p.data + b15->payload + 4 * i, 4);
            if (v != expect[i]) { ok = false; break; }
        }
    }
    std::string detail = b15 ? ("6015 bytes = " + std::to_string(b15->len))
                             : std::string("6015 absent");
    if (b15 && b15->len >= 4) {
        detail += " [";
        for (size_t i = 0; i < b15->len / 4; ++i) {
            uint32_t v = 0;
            std::memcpy(&v, p.data + b15->payload + 4 * i, 4);
            detail += (i ? "," : "") + std::to_string(v);
        }
        detail += "]";
    }
    check(ok, "bone-batch slots name the Bones in skeleton order", detail);
}
} // namespace
namespace {
fs::path write_temp(const av::PODModel& model, const std::string& name) {
    fs::path dir = fs::temp_directory_path() / "swordigo_pod_contract";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path out = dir / name;
    std::string err;
    if (!av::pod_write(model, out.string(), &err)) {
        std::printf("  !! pod_write failed: %s\n", err.c_str());
        return {};
    }
    return out;
}

std::vector<uint8_t> slurp(const fs::path& p) {
    std::vector<uint8_t> out;
    FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize((size_t)n);
        if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
    }
    std::fclose(f);
    return out;
}

// ─── The checks ───────────────────────────────────────────────────────

void check_written_file(const fs::path& path, bool expect_matrix_node,
                        const float* matrix) {
    std::vector<uint8_t> buf = slurp(path);
    if (buf.empty()) {
        check(false, "written POD is readable", path.string());
        return;
    }
    Parsed p;
    if (!walk(buf, p)) {
        check(false, "tag stream stays in sync", path.string());
        return;
    }

    // 0. The file must be a stock POD at the byte-grammar level: every block,
    //    leaf and container alike, bracketed by its own <tag|0x80000000> <0>
    //    close pair.  See OpenSwordigo/PVRToolsDecomp/POD_WRITER_GRAMMAR.md.
    {
        GrammarReport g;
        const size_t end = grammar_scan(buf, 0, g);
        std::string detail = std::to_string(g.blocks) + " blocks, "
                           + std::to_string(g.leaves) + " leaves";
        if (!g.first_problem.empty()) detail += " — " + g.first_problem;
        check(end == buf.size() && g.missing_close == 0 && g.bad_close_tag == 0 &&
                  g.nonzero_close == 0 && g.blocks > 0,
              "every block carries its <tag|0x80000000> <0> close pair", detail);

        // The version block is fixed and always first: tag 1000, len 11,
        // "AB.POD.2.0\0", close.  27 bytes, byte-for-byte what SavePOD emits.
        const uint8_t kHeader[27] = {
            0xE8, 0x03, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00,
            'A', 'B', '.', 'P', 'O', 'D', '.', '2', '.', '0', 0x00,
            0xE8, 0x03, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00 };
        check(buf.size() >= sizeof kHeader &&
                  std::memcmp(buf.data(), kHeader, sizeof kHeader) == 0,
              "file opens with the stock 1000 / AB.POD.2.0 version block");

        // 1002 / 1003 sit between it and the scene block in every stock export.
        const bool has_1002 = count_tag(p, 1002) == 1;
        const bool has_1003 = count_tag(p, 1003) == 1;
        check(has_1002 && has_1003,
              "exporter-options (1002) and provenance (1003) blocks are present");
    }

    // 1. The dead tag must never appear.
    check(count_tag(p, 5010) == 0,
          "no node uses tag 5010 (the runtime discards it)",
          "5010 count = " + std::to_string(count_tag(p, 5010)));

    // 2. Every node carries a transform the reader honours.
    const std::vector<Span> nodes = spans_of(p, 2013);
    check(!nodes.empty(), "file contains at least one node", path.string());

    int missing = 0, matrix_nodes = 0;
    for (const Span& s : nodes) {
        bool has_trs = s.count(5004) || s.count(5005) || s.count(5006);
        bool has_track = s.count(5007) || s.count(5008) || s.count(5009);
        const TagHit* m = s.find(5011);
        bool has_m = m && m->len == 64;
        if (has_m) ++matrix_nodes;
        if (!has_trs && !has_track && !has_m) ++missing;
    }
    check(missing == 0,
          "every node has a transform the game will read",
          std::to_string(missing) + " of " + std::to_string(nodes.size()) +
              " nodes had none");

    if (expect_matrix_node) {
        check(matrix_nodes == 1, "the raw matrix went out as a 64-byte 5011",
              "matrix nodes = " + std::to_string(matrix_nodes));

        // 3. And it is byte-exact.
        const std::vector<const TagHit*> hits = all_of(p, 5011);
        bool exact = hits.size() == 1 && hits[0]->len == 64;
        if (exact) {
            for (int i = 0; i < 16; ++i) {
                float got = 0.0f;
                std::memcpy(&got, buf.data() + hits[0]->payload + 4 * i, 4);
                if (std::memcmp(&got, &matrix[i], 4) != 0) { exact = false; break; }
            }
        }
        check(exact, "5011 payload is a byte-for-byte copy of the source matrix");
    }

    // 4/5. Mesh section keeps the stock shape, per mesh.
    const std::vector<Span> meshes = spans_of(p, 2012);
    check(!meshes.empty(), "file contains at least one mesh", path.string());

    const size_t node_count = nodes.size();
    int bad_strips = 0, bad_unpack = 0, bad_index = 0;
    int bad_batch = 0, bad_batch_ref = 0, bad_static_batch = 0;
    std::string batch_detail, batch_ref_detail, static_batch_detail;
    std::string idx_detail;
    for (const Span& m : meshes) {
        const TagHit* strips = m.find(6005);
        if (!strips || u32_at(p, strips) != 0) ++bad_strips;

        const TagHit* unpack = m.find(6020);
        bool unpack_ok = unpack && unpack->len == 64;
        if (unpack_ok) {
            const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            for (int i = 0; i < 16; ++i) {
                float got = 0.0f;
                std::memcpy(&got, buf.data() + unpack->payload + 4 * i, 4);
                if (std::memcmp(&got, &ident[i], 4) != 0) { unpack_ok = false; break; }
            }
        }
        if (!unpack_ok) ++bad_unpack;

        // The runtime sizes its index memcpy from PVRTModelPODCountIndices(),
        // which is numFaces * 3 while nNumStrips == 0, times the declared
        // element width.  Any disagreement silently truncates the draw.
        // FACES is the first CPODData in the mesh, so the first 9002/9003 pair
        // in the span is its stride / payload.
        const uint32_t faces = u32_at(p, m.find(6001));
        const TagHit* stride_hit = m.find(9002);
        const TagHit* data_hit = m.find(9003);
        const uint32_t stride = u32_at(p, stride_hit);
        const uint64_t want = (uint64_t)faces * 3u * (stride ? stride : 2u);
        const uint64_t got = data_hit ? data_hit->len : 0;
        if (stride != 2 || got != want) {
            ++bad_index;
            if (idx_detail.empty())
                idx_detail = "faces=" + std::to_string(faces) +
                             " stride=" + std::to_string(stride) +
                             " bytes=" + std::to_string(got) +
                             " want=" + std::to_string(want);
        }

        // 6. The bone-batch table (6015/6016/6017/6018/6019).
        //
        // Caver::PODLoader::CreateMesh resolves a vertex's bone in two hops:
        // BONEIDX[vertex] indexes 6015, whose entry is the POD NODE INDEX of the
        // bone; CreateSkeleton then turns that into a Skeleton bone index.  The
        // table is not optional — the remap loop's bound is 6016's first entry,
        // read with no null check — so a skinned mesh without it dereferences
        // null inside the shipped loader and the model draws as an empty
        // silhouette.  6018 is the table's capacity, allocated before any
        // BONEIDX value is used to index it.
        //
        // The four 900x tags belonging to a CPODData container follow that
        // container's own tag, so scan forward from 6012 rather than relying on
        // Span::find (which would return the FACES stream's sub-tags).
        uint32_t boneidx_n = 0;
        const TagHit* boneidx_data = nullptr;
        {
            bool in_boneidx = false;
            for (const TagHit* t : m.tags) {
                if (t->tag == 6012) { in_boneidx = true; continue; }
                if (!in_boneidx) continue;
                if (t->tag == 9001) std::memcpy(&boneidx_n, p.data + t->payload, 4);
                else if (t->tag == 9003) { boneidx_data = t; break; }
            }
        }
        const TagHit* b18 = m.find(6018);
        const TagHit* b19 = m.find(6019);
        const TagHit* b15 = m.find(6015);
        const TagHit* b16 = m.find(6016);
        const TagHit* b17 = m.find(6017);

        if (boneidx_n > 0) {
            uint32_t slots = b16 ? 0u : 0u;
            if (b16 && b16->len >= 4) std::memcpy(&slots, p.data + b16->payload, 4);
            const size_t table_len = b15 ? b15->len / 4 : 0;
            const uint32_t capacity = u32_at(p, b18);
            bool ok = b15 && b16 && b17 && b18 && b19 &&
                      slots >= 1 && table_len >= slots &&
                      capacity >= slots && u32_at(p, b19) >= 1 && b17->len >= 4;
            if (!ok) {
                ++bad_batch;
                if (batch_detail.empty())
                    batch_detail = "slots=" + std::to_string(slots) +
                                   " table=" + std::to_string(table_len) +
                                   " capacity=" + std::to_string(capacity);
            }
            // Every entry of 6015 must name a real node, and every vertex must
            // reference a slot inside the table.
            if (ok) {
                for (size_t i = 0; i < table_len; ++i) {
                    uint32_t v = 0;
                    std::memcpy(&v, p.data + b15->payload + 4 * i, 4);
                    if (v >= node_count) {
                        ++bad_batch_ref;
                        if (batch_ref_detail.empty())
                            batch_ref_detail = "6015[" + std::to_string(i) +
                                               "] = " + std::to_string(v) +
                                               " but the file has " +
                                               std::to_string(node_count) + " nodes";
                        break;
                    }
                }
                if (boneidx_data) {
                    const size_t n_verts = u32_at(p, m.find(6000));
                    for (size_t v = 0; v < n_verts; ++v) {
                        int32_t idx = 0;
                        if (4 * v + 4 > boneidx_data->len) break;
                        std::memcpy(&idx, p.data + boneidx_data->payload + 4 * v, 4);
                        if (idx < 0 || (uint32_t)idx >= slots) {
                            ++bad_batch_ref;
                            if (batch_ref_detail.empty())
                                batch_ref_detail = "BONEIDX[" + std::to_string(v) +
                                                   "] = " + std::to_string(idx) +
                                                   " but the table has " +
                                                   std::to_string(slots) + " slot(s)";
                            break;
                        }
                    }
                }
            }
        } else if (u32_at(p, b18) != 0 || u32_at(p, b19) != 0) {
            ++bad_static_batch;
            if (static_batch_detail.empty())
                static_batch_detail = "6018=" + std::to_string(u32_at(p, b18)) +
                                      " 6019=" + std::to_string(u32_at(p, b19));
        }
    }
    check(bad_strips == 0, "every mesh declares 6005 NumStrips == 0",
          std::to_string(bad_strips) + " of " + std::to_string(meshes.size()) + " wrong");
    check(bad_unpack == 0, "every mesh carries a 64-byte identity 6020 UnpackMatrix",
          std::to_string(bad_unpack) + " of " + std::to_string(meshes.size()) + " wrong");
    check(bad_index == 0, "every index stream == numFaces * 3 * indexWidth", idx_detail);
    check(bad_batch == 0,
          "every skinned mesh carries a complete bone-batch table", batch_detail);
    check(bad_batch_ref == 0,
          "every BONEIDX value is within the batch table", batch_ref_detail);
    check(bad_static_batch == 0,
          "every static mesh declares 6018/6019 == 0 (stock shape)",
          static_batch_detail);

    // An empty CPODData stream must declare its type/count/stride and stop.
    // Stock does not attach a 9003 payload to a stream with n == 0 — see
    // rock1.POD's TANGENT/BINORMAL/COLORS blocks, which are three tags long —
    // and PVRShamanGUI's sub_74BB90 returns without writing anything when its
    // source pointer is NULL.
    size_t empty_with_payload = 0;
    std::string empty_detail;
    for (const TagHit& hr : p.tags) {
        const TagHit* h = &hr;
        if (h->tag != 6006 && h->tag != 6007 && h->tag != 6008 &&
            h->tag != 6009 && h->tag != 6010 && h->tag != 6011 &&
            h->tag != 6012 && h->tag != 6013) continue;
        // Fields of this attribute block, up to the next attribute/container.
        std::vector<const TagHit*> fields;
        for (const TagHit& t : p.tags) {
            if (t.payload <= h->payload) continue;
            if (t.tag >= 6000 && t.tag <= 6020) break;
            if (t.tag >= 9000 && t.tag <= 9003) fields.push_back(&t);
        }
        const TagHit* n_tag = nullptr;
        const TagHit* data_tag = nullptr;
        for (const TagHit* f : fields) {
            if (f->tag == 9001) n_tag = f;
            if (f->tag == 9003) data_tag = f;
        }
        if (n_tag && data_tag && u32_at(p, n_tag) == 0) {
            ++empty_with_payload;
            if (empty_detail.empty())
                empty_detail = "tag " + std::to_string(h->tag) +
                               " declares n == 0 but carries a 9003 payload";
        }
    }
    check(empty_with_payload == 0,
          "an empty attribute stream carries no 9003 payload", empty_detail);

    // The material must carry the full stock tag set.  The nine auxiliary
    // texture slots (3009..3017) matter most: omitted, they fall through to
    // the calloc'd default 0 — a *valid* texture index — where stock writes
    // the -1 sentinel meaning "this material has no texture of that kind".
    // Counted globally rather than per material: 3026 is written before 3000
    // inside each material block (that is the order stock uses), so a
    // per-material forward scan starting at 3000 would step over it.
    const size_t n_materials = all_of(p, 3000).size();
    size_t tex_slots = 0;
    for (const TagHit& t : p.tags)
        if (t.tag >= 3009 && t.tag <= 3017) ++tex_slots;
    check(n_materials > 0 && tex_slots == 9 * n_materials,
          "every material declares all nine auxiliary texture slots",
          std::to_string(tex_slots) + " slots over " +
              std::to_string(n_materials) + " material(s)");
    check(all_of(p, 3026).size() == n_materials,
          "every material declares its 3026 tail flag",
          std::to_string(all_of(p, 3026).size()) + " of " +
              std::to_string(n_materials));
    // The nine slots are the -1 sentinel, not the calloc'd 0 that would bind
    // the file's first texture as a bump/specular/emissive map.
    size_t bad_slot = 0;
    for (const TagHit& t : p.tags) {
        if (t.tag < 3009 || t.tag > 3017 || t.len < 4) continue;
        if (u32_at(p, &t) != 0xFFFFFFFFu) ++bad_slot;
    }
    check(bad_slot == 0,
          "every auxiliary texture slot is the -1 sentinel",
          std::to_string(bad_slot) + " wrong");
}

void check_load_round_trip(const fs::path& path) {
    av::PODModel back = av::pod_load(path.string());
    check(!back.nodes.empty(), "pod_load reads the file back");
    if (back.nodes.empty()) return;
    const av::PODNode& n = back.nodes[0];
    bool same = n.has_matrix;
    if (same) {
        for (int i = 0; i < 16; ++i) {
            if (std::memcmp(&n.matrix[i], &kSketchfabMatrix[i], 4) != 0) { same = false; break; }
        }
    }
    check(same, "5011 round-trips through our loader as a static matrix");

    av::PODModel ref = make_matrix_node_model();
    check(back.meshes.size() == 1 && back.meshes[0].num_faces == 1,
          "mesh face count survives the round trip");
    (void)ref;
}

// Real, previously-broken files: the strongest available check, and skipped
// loudly when this machine has no converted assets.
void check_real_assets() {
    if (const char* env = std::getenv("SWORDIGO_POD_ASSETS")) {
        std::error_code ec;
        if (fs::exists(env, ec)) {
            for (const auto& e : fs::directory_iterator(env, ec)) {
                if (e.is_regular_file() && e.path().extension() == ".POD") {
                    check_written_file(e.path(), /*expect_matrix_node=*/false, nullptr);
                }
            }
        }
        return;
    }
    const char* home = std::getenv("HOME");
    if (!home) return;
    const fs::path dir =
        fs::path(home) / ".local/share/swordigo-desktop/assets/resources";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;

    // Only files our converter wrote (the sidecar stamp proves provenance);
    // stock game assets are read from the APK and may legitimately differ.
    int scanned = 0;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".POD") continue;
        if (!fs::exists(e.path().string() + ".meta", ec)) continue;
        check_written_file(e.path(), /*expect_matrix_node=*/false, nullptr);
        if (++scanned >= 6) break;
    }
    if (scanned == 0) {
        std::printf("  ..   no converter-stamped PODs under %s (skipped)\n",
                    dir.string().c_str());
    }
}

} // namespace

int main() {
    std::printf("pod_game_reader_contract_test — our writer must emit tags the "
                "GAME reads, not just the ones we read\n\n");

    const fs::path synthetic = write_temp(make_matrix_node_model(), "matrix_node.POD");
    if (!synthetic.empty()) {
        std::printf("%s\n", synthetic.string().c_str());
        check_written_file(synthetic, /*expect_matrix_node=*/true, kSketchfabMatrix);
        check_load_round_trip(synthetic);
    }

    // A skinned mesh must carry the batch table whether or not the producer
    // filled one in: the game's loader dereferences 6016[0] unconditionally.
    for (bool supplied : {true, false}) {
        const fs::path sk =
            write_temp(make_skinned_model(supplied),
                       supplied ? "skinned_table.POD" : "skinned_synth.POD");
        if (sk.empty()) continue;
        std::printf("%s  (%s)\n", sk.string().c_str(),
                    supplied ? "table supplied" : "table synthesised");
        check_written_file(sk, /*expect_matrix_node=*/false, nullptr);
        // A supplied table is taken verbatim ({Bone_Hips, Bone_Foot}); a
        // synthesised one reproduces the game's marking rule, which also pulls
        // in the mesh root because it is an ANCESTOR of a bone.
        check_bone_batch_slots(sk, supplied ? std::vector<uint32_t>{1, 2}
                                             : std::vector<uint32_t>{0, 1, 2});
    }

    std::printf("\nconverted assets on this machine\n");
    check_real_assets();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
