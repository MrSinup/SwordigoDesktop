// rubymesh.cpp — RubyMesh binary (.rbm) collision-zone data layer.
//
// File layout (little-endian):
//
//   Header (32 bytes fixed)
//     0x00 u32 magic       = 0x52424D21 ("RBM!")
//     0x04 u16 version     = 1
//     0x06 u16 flags       = 0 (reserved)
//     0x08 u32 modelNameLen
//     0x0C u32 zoneCount
//     0x10 u32 dataOffset  (>= 32, byte offset of the first zone record)
//     0x14 u32 checksum    (CRC32 of every byte after the header)
//     0x18 u64 padding     = 0
//
//   Model-name block      (immediately after the header)
//     modelNameLen UTF-8 bytes, zero-padded to 4-byte alignment
//
//   Zone array            (at dataOffset, packed contiguously)
//     u16  zoneNameLen    zoneName bytes  pad to 4-byte alignment
//     u32  flags          bit0 enabled, bit1 invisible(ruby_transparent),
//                         bit2 reserved, bits3..7 SurfaceMaterial (0..7)
//     f32  worldZ         f32 depthMin     f32 depthMax
//     char[64] top_texture stem (NUL-terminated, "" = ruby_transparent)
//     char[64] front_texture stem (NUL-terminated)
//     u32  vertexCount    vertexCount * (f32 x, f32 y)

#include "tools/rubymesh.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace rbm {

namespace {

// ── Format constants ──────────────────────────────────────────────────────
// Stored little-endian, so the value that makes the on-disk bytes read
// 'R','B','M','!' is 0x214D4252 (the spec's "0x52424D21" is the big-endian
// reading of the same four ASCII bytes).
constexpr uint32_t kMagic          = 0x214D4252u; // on disk: "RBM!"
constexpr uint16_t kVersion        = 1u;
constexpr uint32_t kHeaderSize     = 32u;
constexpr size_t   kTextureStemLen = 64u;  // char[64] fixed field
constexpr uint32_t kMaxZoneNameLen = 4096u;

// ── CRC32 (IEEE, reflected, zlib polynomial 0xEDB88320) ───────────────────
// Computed over all bytes after the header (offset 32 -> EOF). Implemented
// here so rubymesh stays dependency-free (no zlib).
constexpr uint32_t kCrcPoly = 0xEDB88320u;

static uint32_t s_crc_table[256];
static const bool s_crc_init = [] {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1u) ? (kCrcPoly ^ (c >> 1u)) : (c >> 1u);
        s_crc_table[i] = c;
    }
    return true;
}();

uint32_t crc32_bytes(const uint8_t* data, size_t len) {
    (void)s_crc_init;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        crc = s_crc_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8u);
    return crc ^ 0xFFFFFFFFu;
}

// ── Little-endian serialization helpers ────────────────────────────────────
void wr_u8(std::string& b, uint8_t v) { b.push_back(static_cast<char>(v)); }
void wr_u16(std::string& b, uint16_t v) {
    wr_u8(b, static_cast<uint8_t>(v & 0xFFu));
    wr_u8(b, static_cast<uint8_t>((v >> 8u) & 0xFFu));
}
void wr_u32(std::string& b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        wr_u8(b, static_cast<uint8_t>((v >> (8u * i)) & 0xFFu));
}
void wr_f32(std::string& b, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    wr_u32(b, bits);
}
void wr_bytes(std::string& b, const void* p, size_t n) {
    const char* c = static_cast<const char*>(p);
    b.append(c, n);
}

// Bounds-checked little-endian reader.
struct Reader {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    size_t         pos  = 0;

    bool need(size_t n) const { return pos + n <= size; }
    bool u8(uint8_t& out) {
        if (!need(1)) return false;
        out = data[pos++];
        return true;
    }
    bool u16(uint16_t& out) {
        if (!need(2)) return false;
        out = static_cast<uint16_t>(data[pos]) |
              static_cast<uint16_t>(data[pos + 1]) << 8u;
        pos += 2;
        return true;
    }
    bool u32(uint32_t& out) {
        if (!need(4)) return false;
        out = 0;
        for (int i = 0; i < 4; ++i)
            out |= static_cast<uint32_t>(data[pos + i]) << (8u * i);
        pos += 4;
        return true;
    }
    bool f32(float& out) {
        uint32_t bits;
        if (!u32(bits)) return false;
        std::memcpy(&out, &bits, sizeof(out));
        return true;
    }
    bool bytes(void* out, size_t n) {
        if (!need(n)) return false;
        if (n) std::memcpy(out, data + pos, n);
        pos += n;
        return true;
    }
    bool skip(size_t n) {
        if (!need(n)) return false;
        pos += n;
        return true;
    }
};

bool read_string(Reader& r, size_t len, std::string& out) {
    if (len > kMaxZoneNameLen) return false;
    if (!r.need(len)) return false;
    out.assign(reinterpret_cast<const char*>(r.data + r.pos), len);
    r.pos += len;
    return true;
}

// Fixed char[64] stems are stored NUL-terminated; a 64-byte all-zero field is
// an empty stem.
bool read_stem(Reader& r, std::string& out) {
    uint8_t raw[kTextureStemLen];
    if (!r.bytes(raw, kTextureStemLen)) return false;
    out.clear();
    for (size_t i = 0; i < kTextureStemLen; ++i) {
        if (raw[i] == 0) break;
        out.push_back(static_cast<char>(raw[i]));
    }
    return true;
}

void write_stem(std::string& b, const std::string& stem) {
    uint8_t raw[kTextureStemLen] = {0};
    const size_t n = std::min(stem.size(), kTextureStemLen - 1); // keep NUL room
    if (n) std::memcpy(raw, stem.data(), n);
    wr_bytes(b, raw, kTextureStemLen);
}

uint32_t flags_from_zone(const Zone& z) {
    uint32_t f = 0;
    if (z.enabled)   f |= 0x1u;
    if (z.invisible) f |= 0x2u;
    f |= (static_cast<uint32_t>(z.material) & 0x1Fu) << 3u;
    return f;
}

void zone_from_flags(uint32_t f, Zone& z) {
    z.enabled   = (f & 0x1u) != 0;
    z.invisible = (f & 0x2u) != 0;
    z.material  = static_cast<SurfaceMaterial>((f >> 3u) & 0x1Fu);
}

// Serialize one zone record (does NOT include inter-zone alignment).
uint32_t rd_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) | (static_cast<uint32_t>(p[3]) << 24u);
}
uint16_t rd_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8u);
}

std::string serialize_zone(const Zone& z) {
    std::string rec;
    const size_t name_len = std::min<size_t>(z.name.size(), 0xFFFFu);
    wr_u16(rec, static_cast<uint16_t>(std::min<size_t>(name_len, 0xFFFFu)));
    if (name_len) wr_bytes(rec, z.name.data(), name_len);
    // Pad the (2 + name) prefix to 4-byte alignment.
    const size_t pad = (4u - (2u + name_len) % 4u) % 4u;
    for (size_t i = 0; i < pad; ++i) wr_u8(rec, 0);
    wr_u32(rec, flags_from_zone(z));
    wr_f32(rec, z.world_z);
    wr_f32(rec, z.depth_min);
    wr_f32(rec, z.depth_max);
    write_stem(rec, z.top_texture);
    write_stem(rec, z.front_texture);
    wr_u32(rec, static_cast<uint32_t>(z.vertices.size()));
    for (const auto& v : z.vertices) {
        wr_f32(rec, v.first);
        wr_f32(rec, v.second);
    }
    return rec;
}

// Deserialize one zone record at the reader position. Returns false on
// truncation or absurd lengths.
bool deserialize_zone(Reader& r, Zone& z) {
    uint16_t name_len = 0;
    if (!r.u16(name_len)) return false;
    if (!read_string(r, name_len, z.name)) return false;
    const size_t pad = (4u - (2u + name_len) % 4u) % 4u;
    if (!r.skip(pad)) return false;
    uint32_t flags = 0;
    if (!r.u32(flags)) return false;
    zone_from_flags(flags, z);
    if (!r.f32(z.world_z)) return false;
    if (!r.f32(z.depth_min)) return false;
    if (!r.f32(z.depth_max)) return false;
    if (!read_stem(r, z.top_texture)) return false;
    if (!read_stem(r, z.front_texture)) return false;
    uint32_t count = 0;
    if (!r.u32(count)) return false;
    // Guard: every vertex needs 8 bytes; reject absurd counts up front.
    if (static_cast<size_t>(count) > (r.size - r.pos) / 8u) return false;
    z.vertices.clear();
    z.vertices.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        float x = 0.0f, y = 0.0f;
        if (!r.f32(x)) return false;
        if (!r.f32(y)) return false;
        z.vertices.emplace_back(x, y);
    }
    return true;
}

} // namespace

// ── File paths ─────────────────────────────────────────────────────────────

std::string rbm_path_for(const std::string& model_stem,
                         const std::string& scene_dir) {
    // Defensive: a stem is a bare model name — drop any directory components.
    std::string stem = model_stem;
    const size_t slash = stem.find_last_of("/\\");
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    std::string dir = scene_dir;
    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    if (dir.empty()) return stem + "_rubymesh.rbm";
    return dir + "/" + stem + "_rubymesh.rbm";
}

// ── Polygon orientation helpers ────────────────────────────────────────────

double polygon_signed_area(const std::vector<std::pair<float, float>>& verts) {
    const size_t n = verts.size();
    if (n < 3) return 0.0;
    double a = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        a += static_cast<double>(verts[i].first)  * verts[j].second -
             static_cast<double>(verts[j].first)  * verts[i].second;
    }
    return a * 0.5;
}

void ensure_ccw(std::vector<std::pair<float, float>>& verts) {
    if (polygon_signed_area(verts) < 0.0)
        std::reverse(verts.begin(), verts.end());
}

// ── I/O ────────────────────────────────────────────────────────────────────

bool rbm_save(const RubyMesh& mesh, const std::string& path, std::string& err) {
    err.clear();
    if (path.empty()) { err = "empty .rbm path"; return false; }

    // Body = model name block + zone array (everything after the header). The
    // checksum covers this whole region.
    std::string body;
    const size_t name_len = mesh.model_name.size();
    if (name_len) wr_bytes(body, mesh.model_name.data(), name_len);
    const size_t name_pad = (4u - name_len % 4u) % 4u;
    for (size_t i = 0; i < name_pad; ++i) wr_u8(body, 0);
    const uint32_t data_offset = kHeaderSize + static_cast<uint32_t>(name_len + name_pad);
    for (const auto& zone : mesh.zones)
        body += serialize_zone(zone);

    std::string hdr;
    wr_u32(hdr, kMagic);
    wr_u16(hdr, kVersion);
    wr_u16(hdr, 0);                    // flags: reserved
    wr_u32(hdr, static_cast<uint32_t>(name_len));
    wr_u32(hdr, static_cast<uint32_t>(mesh.zones.size()));
    wr_u32(hdr, data_offset);
    const uint64_t crc = crc32_bytes(
        reinterpret_cast<const uint8_t*>(body.data()), body.size());
    wr_u32(hdr, static_cast<uint32_t>(crc));
    for (int i = 0; i < 8; ++i) wr_u8(hdr, 0);  // padding
    const std::string file = hdr + body;

    // Atomic write: temp file + rename (POSIX rename within one directory is
    // atomic; the editor never runs this on exotic filesystems).
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { err = "cannot create " + tmp; return false; }
        out.write(file.data(), static_cast<std::streamsize>(file.size()));
        out.flush();
        if (!out) { err = "write failed: " + tmp; return false; }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        err = "rename failed: " + tmp + " -> " + path;
        return false;
    }

    // Verify magic + checksum of the written file.
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) { err = "cannot re-open written file " + path; return false; }
        std::string back((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        if (back.size() < kHeaderSize || rd_u32_le(reinterpret_cast<const uint8_t*>(back.data())) != kMagic) {
            err = "verify failed: bad magic after write";
            return false;
        }
        const uint64_t stored = crc32_bytes(
            reinterpret_cast<const uint8_t*>(back.data() + kHeaderSize),
            back.size() - kHeaderSize);
        const uint32_t expected = rd_u32_le(
            reinterpret_cast<const uint8_t*>(back.data() + 0x14));
        if (stored != expected) {
            err = "verify failed: checksum mismatch after write";
            return false;
        }
    }
    return true;
}

bool rbm_load(const std::string& path, RubyMesh& out, std::string& err) {
    err.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "cannot open " + path; return false; }
    std::string bytes((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    if (bytes.size() < kHeaderSize) {
        err = path + ": file too small for an .rbm header";
        return false;
    }
    const uint8_t* b = reinterpret_cast<const uint8_t*>(bytes.data());
    const size_t size = bytes.size();

    const uint32_t magic = rd_u32_le(b);
    if (magic != kMagic) { err = path + ": bad magic (not an .rbm file?)"; return false; }

    const uint16_t version = rd_u16_le(b + 0x04);
    if (version != kVersion) { err = path + ": unsupported .rbm version " + std::to_string(version); return false; }

    const uint32_t name_len    = rd_u32_le(b + 0x08);
    const uint32_t zone_count  = rd_u32_le(b + 0x0C);
    const uint32_t data_offset = rd_u32_le(b + 0x10);

    // Checksum covers everything after the 32-byte header.
    const uint32_t stored_crc = rd_u32_le(b + 0x14);
    const uint32_t actual_crc = crc32_bytes(b + kHeaderSize, size - kHeaderSize);
    if (actual_crc != stored_crc) {
        err = path + ": checksum mismatch (file corrupt or truncated)";
        return false;
    }

    if (data_offset < kHeaderSize || data_offset > size) {
        err = path + ": invalid data offset";
        return false;
    }
    if (name_len > kMaxZoneNameLen || name_len > data_offset - kHeaderSize) {
        err = path + ": invalid model name length";
        return false;
    }

    RubyMesh mesh;
    mesh.model_name.assign(bytes.data() + kHeaderSize, name_len);

    Reader r;
    r.data = b;
    r.size = size;
    r.pos  = data_offset;

    // Guard against absurd zone counts: every zone consumes at least
    // 2 + pad(2) + 4 + 12 + 128 + 4 = ~152 bytes of record.
    if (static_cast<size_t>(zone_count) > (size - data_offset) / 150u + 1u) {
        err = path + ": zone count exceeds file size";
        return false;
    }

    mesh.zones.reserve(zone_count);
    for (uint32_t i = 0; i < zone_count; ++i) {
        Zone z;
        if (!deserialize_zone(r, z)) {
            err = path + ": truncated zone record " + std::to_string(i);
            return false;
        }
        mesh.zones.push_back(std::move(z));
    }

    out = std::move(mesh);
    return true;
}

RubyMesh rbm_load_or_default(const std::string& model_stem,
                             const std::string& scene_dir) {
    RubyMesh mesh;
    mesh.model_name = model_stem;
    std::string err;
    const std::string path = rbm_path_for(model_stem, scene_dir);
    if (rbm_load(path, mesh, err)) return mesh;
    if (!err.empty())
        std::fprintf(stderr, "[RubyMesh] %s — starting with an empty zone set.\n",
                     err.c_str());
    return mesh;
}

} // namespace rbm
