/* zip_archive.cpp — shared minimal ZIP reader/writer (see zip_archive.h).
 *
 * Moved verbatim from ruby_cli.cpp's inline `namespace zip` (master TODO 4.1)
 * with two additions: `external_attr` round-trip on read/write, and
 * extract_all() (the CLI's apk_extract loop, made reusable by the GUI's APK
 * session import). Uses zlib raw inflate/deflate for method 8 entries.
 */

#include "platform/zip_archive.h"

#include <zlib.h>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace zip {

namespace {

#pragma pack(push, 1)
struct LocalHeader {
    uint32_t sig;        // 0x04034b50
    uint16_t version_needed;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
};
struct CentralHeader {
    uint32_t sig;        // 0x02014b50
    uint16_t version_made;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint16_t disk_start;
    uint16_t internal_attr;
    uint32_t external_attr;
    uint32_t local_offset;
};
#pragma pack(pop)

uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
void wr16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x & 0xff)); v.push_back((uint8_t)(x >> 8));
}
void wr32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x & 0xff)); v.push_back((uint8_t)((x >> 8) & 0xff));
    v.push_back((uint8_t)((x >> 16) & 0xff)); v.push_back((uint8_t)((x >> 24) & 0xff));
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_file(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return f.good();
}

bool inflate_raw(const std::string& in, std::string& out, size_t expected) {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
    zs.next_in  = (Bytef*)in.data();
    zs.avail_in = (uInt)in.size();
    out.resize(expected);
    zs.next_out  = (Bytef*)out.data();
    zs.avail_out = (uInt)out.size();
    int rc = inflate(&zs, Z_FINISH);
    bool ok = (rc == Z_STREAM_END || (rc == Z_OK && zs.avail_out == 0));
    if (!ok) out.clear();
    inflateEnd(&zs);
    return ok;
}

bool deflate_raw(const std::string& in, std::string& out) {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    zs.next_in  = (Bytef*)in.data();
    zs.avail_in = (uInt)in.size();
    std::vector<uint8_t> buf(65536);
    int rc;
    do {
        zs.next_out  = buf.data();
        zs.avail_out = (uInt)buf.size();
        rc = deflate(&zs, Z_FINISH);
        size_t got = buf.size() - zs.avail_out;
        if (got) out.append((const char*)buf.data(), got);
    } while (rc == Z_OK);
    deflateEnd(&zs);
    return rc == Z_STREAM_END;
}

} // namespace

// ─── Reader ──────────────────────────────────────────────────────────────────

bool read_entries(const std::string& zip_path, std::vector<Entry>& entries) {
    std::string buf;
    if (!read_file(zip_path, buf)) return false;
    if (buf.size() < 22) return false;

    // Locate end-of-central-directory within last 64 KiB + 22.
    size_t eocd = std::string::npos;
    size_t start = buf.size() >= (22 + 65535) ? buf.size() - (22 + 65535) : 0;
    for (size_t i = buf.size() - 22 + 1; i-- > start;) {
        if ((uint8_t)buf[i] == 0x50 && (uint8_t)buf[i + 1] == 0x4b &&
            (uint8_t)buf[i + 2] == 0x05 && (uint8_t)buf[i + 3] == 0x06) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) return false;

    const uint8_t* e = (const uint8_t*)buf.data() + eocd;
    uint16_t count = rd16(e + 10);
    uint32_t cd_off = rd32(e + 16);

    for (uint16_t n = 0; n < count; ++n) {
        if (cd_off + 46 > buf.size()) return false;
        const uint8_t* c = (const uint8_t*)buf.data() + cd_off;
        if (rd32(c) != 0x02014b50) return false;
        Entry ent;
        ent.method      = rd16(c + 10);
        ent.crc32       = rd32(c + 16);
        ent.comp_size   = rd32(c + 20);
        ent.uncomp_size = rd32(c + 24);
        uint16_t name_len  = rd16(c + 28);
        uint16_t extra_len = rd16(c + 30);
        uint16_t comm_len  = rd16(c + 32);
        ent.external_attr  = rd32(c + 38);
        uint32_t local_off = rd32(c + 42);
        if (cd_off + 46 + name_len > buf.size()) return false;
        ent.name.assign((const char*)(c + 46), name_len);
        ent.data_offset = local_off;

        // Resolve actual data offset from the local header.
        if (local_off + 30 <= buf.size()) {
            const uint8_t* l = (const uint8_t*)buf.data() + local_off;
            if (rd32(l) == 0x04034b50) {
                uint16_t lname = rd16(l + 26);
                uint16_t lextra = rd16(l + 28);
                ent.data_offset = local_off + 30 + lname + lextra;
            }
        }
        entries.push_back(std::move(ent));
        cd_off += 46 + name_len + extra_len + comm_len;
    }
    return true;
}

bool read_entry(const std::string& zip_path, const Entry& e, std::string& out) {
    std::string buf;
    if (!read_file(zip_path, buf)) return false;
    if (e.data_offset + e.comp_size > buf.size()) return false;
    std::string raw = buf.substr(e.data_offset, e.comp_size);
    if (e.method == 0) {
        out = raw;
        return out.size() == e.uncomp_size;
    }
    if (e.method == 8) return inflate_raw(raw, out, e.uncomp_size);
    return false;
}

// ─── Writer ──────────────────────────────────────────────────────────────────

bool write_archive(const std::string& out_path,
                   const std::vector<OutEntry>& in_entries) {
    std::vector<OutEntry> entries = in_entries;
    std::vector<uint8_t> body;
    std::vector<uint8_t> central;

    uint16_t mtime = 0, mdate = 0;
    std::time_t now = std::time(nullptr);
    std::tm* tmv = std::localtime(&now);
    if (tmv) {
        mtime = (uint16_t)(((uint16_t)tmv->tm_hour << 11) |
                           ((uint16_t)tmv->tm_min << 5) |
                           ((uint16_t)tmv->tm_sec / 2));
        mdate = (uint16_t)((((uint16_t)(tmv->tm_year + 1900) - 1980) << 9) |
                           ((uint16_t)(tmv->tm_mon + 1) << 5) |
                           (uint16_t)tmv->tm_mday);
    }

    uint32_t local_offset = 0;
    for (auto& e : entries) {
        std::string payload;
        uint32_t crc = 0;
        uint32_t comp = 0;
        uint16_t method = e.method;
        if (e.method == 0) {
            payload = e.data;
            comp = (uint32_t)e.data.size();
        } else {
            if (!deflate_raw(e.data, payload)) return false;
            comp = (uint32_t)payload.size();
        }
        crc = (uint32_t)crc32(0L, (const Bytef*)e.data.data(), (uInt)e.data.size());

        LocalHeader lh;
        std::memset(&lh, 0, sizeof(lh));
        lh.sig = 0x04034b50;
        lh.version_needed = 20;
        lh.method = method;
        lh.mod_time = mtime;
        lh.mod_date = mdate;
        lh.crc32 = crc;
        lh.comp_size = comp;
        lh.uncomp_size = (uint32_t)e.data.size();
        lh.name_len = (uint16_t)e.name.size();
        wr32(body, lh.sig);
        wr16(body, lh.version_needed);
        wr16(body, lh.flags);
        wr16(body, lh.method);
        wr16(body, lh.mod_time);
        wr16(body, lh.mod_date);
        wr32(body, lh.crc32);
        wr32(body, lh.comp_size);
        wr32(body, lh.uncomp_size);
        wr16(body, lh.name_len);
        wr16(body, lh.extra_len);
        body.insert(body.end(), e.name.begin(), e.name.end());
        body.insert(body.end(), payload.begin(), payload.end());

        CentralHeader ch;
        std::memset(&ch, 0, sizeof(ch));
        ch.sig = 0x02014b50;
        ch.version_made = 20;
        ch.version_needed = 20;
        ch.method = method;
        ch.mod_time = mtime;
        ch.mod_date = mdate;
        ch.crc32 = crc;
        ch.comp_size = comp;
        ch.uncomp_size = (uint32_t)e.data.size();
        ch.name_len = (uint16_t)e.name.size();
        ch.external_attr = e.external_attr;
        ch.local_offset = local_offset;
        wr32(central, ch.sig);
        wr16(central, ch.version_made);
        wr16(central, ch.version_needed);
        wr16(central, ch.flags);
        wr16(central, ch.method);
        wr16(central, ch.mod_time);
        wr16(central, ch.mod_date);
        wr32(central, ch.crc32);
        wr32(central, ch.comp_size);
        wr32(central, ch.uncomp_size);
        wr16(central, ch.name_len);
        wr16(central, ch.extra_len);
        wr16(central, ch.comment_len);
        wr16(central, ch.disk_start);
        wr16(central, ch.internal_attr);
        wr32(central, ch.external_attr);
        wr32(central, ch.local_offset);
        central.insert(central.end(), e.name.begin(), e.name.end());

        local_offset += 30 + (uint32_t)e.name.size() + comp;
    }

    std::vector<uint8_t> out;
    out.insert(out.end(), body.begin(), body.end());
    uint32_t cd_off = (uint32_t)body.size();
    out.insert(out.end(), central.begin(), central.end());
    // EOCD
    wr32(out, 0x06054b50);
    wr16(out, 0);
    wr16(out, 0);
    wr16(out, (uint16_t)entries.size());
    wr16(out, (uint16_t)entries.size());
    wr32(out, (uint32_t)central.size());
    wr32(out, cd_off);
    wr16(out, 0);

    return write_file(out_path, std::string((char*)out.data(), out.size()));
}

// ─── Whole-archive extract (APK import + CLI `apk extract`) ─────────────────

ExtractResult extract_all(const std::string& zip_path, const std::string& dest_dir) {
    ExtractResult res;
    std::vector<Entry> entries;
    if (!read_entries(zip_path, entries)) {
        res.error = "not a valid ZIP";
        return res;
    }

    std::error_code ec;
    if (!std::filesystem::create_directories(dest_dir, ec) && ec) {
        res.error = "cannot create destination directory";
        return res;
    }

    for (auto& e : entries) {
        // Safe path: strip traversal, reject absolute.
        std::string cleaned;
        for (char c : e.name) cleaned += (c == '\\') ? '/' : c;
        while (cleaned.rfind("../", 0) == 0) cleaned.erase(0, 3);
        if (cleaned.empty() || cleaned[0] == '/') continue;
        if (cleaned.back() == '/') { // directory entry
            std::filesystem::create_directories(std::filesystem::path(dest_dir) / cleaned, ec);
            continue;
        }
        std::string data;
        if (!read_entry(zip_path, e, data)) {
            res.failed.push_back(e.name);
            continue;
        }
        std::filesystem::path target = std::filesystem::path(dest_dir) / cleaned;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (write_file(target.string(), data)) {
            ++res.extracted;
        } else {
            res.failed.push_back(e.name);
        }
    }
    res.ok = true;
    return res;
}

} // namespace zip