#include "tools/pod_convert.h"

#include <zlib.h>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <array>
#include <vector>

#include "tools/pod_loader.h"
#include "tools/fbx_import.h"
#include "tools/gltf_glb.h"
#include "tools/pod_writer.h"
#include "tools/obj_loader.h" // E17: --obj2pod reuses the viewer's OBJ parser
#include "stb/stb_image.h"

namespace fs = std::filesystem;

namespace av {

// ─── Game-compatible PVR header ─────────────────────────────────────────────
// The game's runtime texture uploader (PVRTTextureLoadFromPointer in
// libswordigo) ONLY accepts the legacy 52-byte PVR v2-style header: it keys
// off the first u32 == 52 and rejects any other container (including the
// "PVR\3" PVR v3 header) with "failed: not a valid pvr". The real game .pvr
// assets use exactly this layout, so converter output must mirror it:
//
//   +0   u32 header_size   = 52
//   +4   u32 height
//   +8   u32 width
//   +12  u32 mip_count     = 1
//   +16  u32 flags         = 0x36 (ETC1_RGB8_OES) — low byte is the format
//   +20  u32 data_size     = compressed bytes per surface (used as a stride)
//   +24  u32 bits_per_px   = 4  (bits/pixel, ETC1 / size estimation)
//   +28..u32 masks[4]      = 0
//   +44  u32 magic         = 0x21525650 ("PVR!")
//   +48  u32 num_surfaces  = 1
//   +52  ... ETC1/RGBA pixel data
#pragma pack(push, 1)
struct GamePvrHdr {
    uint32_t header_size = 52;
    uint32_t height      = 0;
    uint32_t width       = 0;
    // Stock game .pvr textures use mip_count = 0 (not 1) — match exactly so the
    // game's PVRTTextureLoadFromPointer sees the same header shape as shipped assets.
    uint32_t mip_count   = 0;
    // Stock header has flags = 0x10036: low byte 0x36 = ETC1_RGB8_OES, plus the
    // 0x10000 "twiddle"/mipmap bit the SDK sets. Reproduce it verbatim.
    uint32_t flags       = 0x10036;
    uint32_t data_size   = 0;
    uint32_t bpp_bits    = 4;
    // Stock masks are 0xFFFFFFFF, not 0. Match the shipped header byte-for-byte.
    uint32_t mask[4]     = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0 };
    uint32_t magic       = 0x21525650; // "PVR!"
    uint32_t num_surfaces = 1;
};
#pragma pack(pop)

static const uint32_t kGamePvrFlagsETC1 = 0x36;

static bool gzip_compress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    strm.next_in  = const_cast<uint8_t*>(in.data());
    strm.avail_in = (uInt)in.size();
    uint8_t buf[65536];
    do {
        strm.next_out  = buf;
        strm.avail_out = sizeof(buf);
        int ret = deflate(&strm, Z_FINISH);
        if (ret == Z_STREAM_ERROR) { deflateEnd(&strm); return false; }
        size_t produced = sizeof(buf) - strm.avail_out;
        out.insert(out.end(), buf, buf + produced);
    } while (strm.avail_out == 0);
    deflateEnd(&strm);
    return true;
}

static bool write_file(const std::string& path, const void* data, size_t size) {
    fs::path p(path);
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = (fwrite(data, 1, size, f) == size);
    fclose(f);
    return ok;
}

// ─── Native ETC1 block encoder (mirrors batch_converter.cpp §3 so pod and
//      batch conversions produce identical tex data) ───────────────────────
static inline int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static const int kETC1Modifiers[8][2] = {
    { 2, 8}, { 5, 17}, { 9, 29}, {13, 42},
    {18,56}, {24, 71}, {33, 92}, {47,127}
};

static void quant555(int r, int g, int b, int& r5, int& g5, int& b5) {
    r5 = clamp8(r) >> 3;
    g5 = clamp8(g) >> 3;
    b5 = clamp8(b) >> 3;
}

static void expand555(int r5, int g5, int b5, int& r, int& g, int& b) {
    r = (r5 << 3) | (r5 >> 2);
    g = (g5 << 3) | (g5 >> 2);
    b = (b5 << 3) | (b5 >> 2);
}

static uint64_t score_subblock(const uint8_t* pixels[8], int br, int bg, int bb,
                               int table_idx, uint8_t selectors[8]) {
    uint64_t err = 0;
    const int* mods = kETC1Modifiers[table_idx];
    for (int i = 0; i < 8; i++) {
        int pr = pixels[i][0], pg = pixels[i][1], pb = pixels[i][2];
        uint64_t best = UINT64_MAX;
        uint8_t  sel  = 0;
        for (uint8_t s = 0; s < 4; s++) {
            int sign = (s < 2) ? 1 : -1;
            int mod  = (s & 1) ? mods[1] : mods[0];
            int dr = clamp8(br + sign*mod) - pr;
            int dg = clamp8(bg + sign*mod) - pg;
            int db = clamp8(bb + sign*mod) - pb;
            uint64_t e = (uint64_t)(dr*dr + dg*dg + db*db);
            if (e < best) { best = e; sel = s; }
        }
        err += best;
        selectors[i] = sel;
    }
    return err;
}

static void encode_etc1_block(const uint8_t* src, int src_stride_bytes, uint8_t* dst) {
    const uint8_t* P[4][4];
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++)
            P[row][col] = src + row * src_stride_bytes + col * 4;

    auto avg_rgb = [](const uint8_t* pp[8], int& ar, int& ag, int& ab) {
        int sr = 0, sg = 0, sb = 0;
        for (int i = 0; i < 8; i++) { sr += pp[i][0]; sg += pp[i][1]; sb += pp[i][2]; }
        ar = sr / 8; ag = sg / 8; ab = sb / 8;
    };

    uint64_t best_err = UINT64_MAX;
    uint64_t best_block = 0;

    for (int flip = 0; flip < 2; flip++) {
        const uint8_t* ppA[8], *ppB[8];
        if (flip == 0) {
            for (int row = 0; row < 4; row++) {
                ppA[row*2+0] = P[row][0]; ppA[row*2+1] = P[row][1];
                ppB[row*2+0] = P[row][2]; ppB[row*2+1] = P[row][3];
            }
        } else {
            for (int col = 0; col < 4; col++) {
                ppA[col*2+0] = P[0][col]; ppA[col*2+1] = P[1][col];
                ppB[col*2+0] = P[2][col]; ppB[col*2+1] = P[3][col];
            }
        }

        int arA, agA, abA, arB, agB, abB;
        avg_rgb(ppA, arA, agA, abA);
        avg_rgb(ppB, arB, agB, abB);

        int r5A, g5A, b5A, r5B, g5B, b5B;
        quant555(arA, agA, abA, r5A, g5A, b5A);
        quant555(arB, agB, abB, r5B, g5B, b5B);

        int dr = r5B - r5A, dg = g5B - g5A, db = b5B - b5A;
        auto clamp3s = [](int v) { return v < -4 ? -4 : (v > 3 ? 3 : v); };
        dr = clamp3s(dr); dg = clamp3s(dg); db = clamp3s(db);
        int r5Beff = r5A + dr, g5Beff = g5A + dg, b5Beff = b5A + db;
        if (r5Beff < 0 || r5Beff > 31 || g5Beff < 0 || g5Beff > 31 || b5Beff < 0 || b5Beff > 31) continue;

        int brA, bgA, bbA, brB, bgB, bbB;
        expand555(r5A, g5A, b5A, brA, bgA, bbA);
        expand555(r5Beff, g5Beff, b5Beff, brB, bgB, bbB);

        for (int tA = 0; tA < 8; tA++) {
            for (int tB = 0; tB < 8; tB++) {
                uint8_t selA[8], selB[8];
                uint64_t eA = score_subblock(ppA, brA, bgA, bbA, tA, selA);
                uint64_t eB = score_subblock(ppB, brB, bgB, bbB, tB, selB);
                uint64_t total_err = eA + eB;
                if (total_err >= best_err) continue;
                best_err = total_err;

                uint64_t word = 0;
                word |= (uint64_t)(r5A & 31) << 59;
                word |= (uint64_t)((uint8_t)(dr & 7)) << 56;
                word |= (uint64_t)(g5A & 31) << 51;
                word |= (uint64_t)((uint8_t)(dg & 7)) << 48;
                word |= (uint64_t)(b5A & 31) << 43;
                word |= (uint64_t)((uint8_t)(db & 7)) << 40;
                word |= (uint64_t)(tA & 7) << 37;
                word |= (uint64_t)(tB & 7) << 34;
                word |= (1ULL << 33);
                if (flip) word |= (1ULL << 32);

                static const uint8_t SEL2MSB[4] = {0, 1, 0, 1};
                static const uint8_t SEL2LSB[4] = {0, 0, 1, 1};

                auto pixel_index = [flip](int sub, int i) -> int {
                    if (flip == 0) {
                        int row = i / 2;
                        int col = (i % 2) + (sub == 1 ? 2 : 0);
                        return col * 4 + row;
                    } else {
                        int col = i / 2;
                        int row = (i % 2) + (sub == 1 ? 2 : 0);
                        return col * 4 + row;
                    }
                };

                for (int i = 0; i < 8; i++) {
                    int pidx = pixel_index(0, i);
                    uint8_t s = selA[i];
                    word |= (uint64_t)SEL2MSB[s] << (16 + pidx);
                    word |= (uint64_t)SEL2LSB[s] << pidx;
                }
                for (int i = 0; i < 8; i++) {
                    int pidx = pixel_index(1, i);
                    uint8_t s = selB[i];
                    word |= (uint64_t)SEL2MSB[s] << (16 + pidx);
                    word |= (uint64_t)SEL2LSB[s] << pidx;
                }
                best_block = word;
            }
        }
    }

    for (int i = 0; i < 8; i++)
        dst[i] = (uint8_t)(best_block >> (56 - i * 8));
}

// High-quality bilinear image resizer for generating HD / custom-resolution textures.
static std::vector<uint8_t> resize_rgba(const uint8_t* src, int sw, int sh, int dw, int dh) {
    if (sw == dw && sh == dh) {
        return std::vector<uint8_t>(src, src + (size_t)sw * sh * 4);
    }
    std::vector<uint8_t> dst((size_t)dw * dh * 4);
    float x_ratio = (float)sw / (float)dw;
    float y_ratio = (float)sh / (float)dh;

    for (int dy = 0; dy < dh; dy++) {
        float sy = (dy + 0.5f) * y_ratio - 0.5f;
        int y0 = std::clamp((int)std::floor(sy), 0, sh - 1);
        int y1 = std::clamp(y0 + 1, 0, sh - 1);
        float fy = sy - std::floor(sy);
        if (fy < 0.0f) fy = 0.0f;

        for (int dx = 0; dx < dw; dx++) {
            float sx = (dx + 0.5f) * x_ratio - 0.5f;
            int x0 = std::clamp((int)std::floor(sx), 0, sw - 1);
            int x1 = std::clamp(x0 + 1, 0, sw - 1);
            float fx = sx - std::floor(sx);
            if (fx < 0.0f) fx = 0.0f;

            const uint8_t* p00 = src + ((size_t)y0 * sw + x0) * 4;
            const uint8_t* p10 = src + ((size_t)y0 * sw + x1) * 4;
            const uint8_t* p01 = src + ((size_t)y1 * sw + x0) * 4;
            const uint8_t* p11 = src + ((size_t)y1 * sw + x1) * 4;

            uint8_t* out = dst.data() + ((size_t)dy * dw + dx) * 4;
            for (int c = 0; c < 4; c++) {
                float top = (float)p00[c] * (1.0f - fx) + (float)p10[c] * fx;
                float bot = (float)p01[c] * (1.0f - fx) + (float)p11[c] * fx;
                float val = top * (1.0f - fy) + bot * fy;
                out[c] = (uint8_t)std::clamp(std::round(val), 0.0f, 255.0f);
            }
        }
    }
    return dst;
}

// Encode a top-first authored RGBA image into the game's texture containers.
// The vertical flip + premultiplied alpha mirror batch_converter.cpp's import
// path and give every uploaded texture the game's bottom-origin (v = 0 bottom)
// layout.
//
// Alpha handling: ETC1 is RGB-only, so any texture carrying a non-opaque alpha
// channel is encoded as uncompressed RGBA8888 (PVR flags 0x12) instead — matching
// batch_converter.cpp's format selection. Opaque textures use ETC1 (0x10036).
//
//  - out_pvr=true:  legacy 52-byte PVR header + ETC1/RGBA data (.pvr) — the exact
//                   layout the game's PVRTTextureLoadFromPointer parses
//                   (header_size==52, flags=0x10036 ETC1 or 0x12 RGBA8888, "PVR!" magic).
//                   ETC1 uploaded through glCompressedTexImage2D; RGBA through glTexImage2D.
//  - out_pvr=false: bytes for a ".tex.png" — the gzip of the native TEX
//                   container {img_type(1=RGBA8888), w, h} + RGBA payload,
//                   which is what the game/viewer background loaders expect.
static bool encode_texture(const uint8_t* rgba, int w, int h, bool out_pvr, int target_res,
                           std::vector<uint8_t>& tex, std::string* err) {
    std::vector<uint8_t> resized_buf;
    if (target_res > 0 && (w != target_res || h != target_res)) {
        int tw = target_res;
        int th = (w > 0) ? (int)std::round((float)target_res * (float)h / (float)w) : target_res;
        if (th <= 0) th = target_res;
        tw = ((tw + 3) / 4) * 4;
        th = ((th + 3) / 4) * 4;
        resized_buf = resize_rgba(rgba, w, h, tw, th);
        rgba = resized_buf.data();
        w = tw;
        h = th;
    }

    // Detect whether the source carries a meaningful alpha channel. ETC1 cannot
    // store alpha, so we must fall back to uncompressed RGBA for any texture that
    // isn't fully opaque (characters, particles, decals, ...).
    bool has_alpha = false;
    for (int i = 0; i < w * h; i++) {
        uint8_t a = rgba[(size_t)i * 4 + 3];
        if (a != 255) { has_alpha = true; break; }
    }

    std::vector<uint8_t> flipped((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
        memcpy(&flipped[(size_t)y * w * 4], &rgba[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);

    // Only premultiply for the ETC1 (opaque) path. RGBA8888 keeps straight alpha.
    if (!has_alpha) {
        for (int i = 0; i < w * h; i++) {
            uint8_t a = flipped[i * 4 + 3];
            if (a == 0) {
                flipped[i * 4 + 0] = 0;
                flipped[i * 4 + 1] = 0;
                flipped[i * 4 + 2] = 0;
            } else if (a != 255) {
                flipped[i * 4 + 0] = (uint8_t)(((uint32_t)flipped[i * 4 + 0] * a) / 255);
                flipped[i * 4 + 1] = (uint8_t)(((uint32_t)flipped[i * 4 + 1] * a) / 255);
                flipped[i * 4 + 2] = (uint8_t)(((uint32_t)flipped[i * 4 + 2] * a) / 255);
            }
        }
    }

    if (out_pvr) {
        if (has_alpha) {
            // Uncompressed RGBA8888 for alpha-carrying textures. Header flags
            // 0x12 = RGBA8888 in the game's legacy PVR loader (batch_converter.cpp).
            GamePvrHdr hdr;
            hdr.height    = (uint32_t)h;
            hdr.width     = (uint32_t)w;
            hdr.flags     = 0x12;          // RGBA8888
            hdr.bpp_bits  = 32;
            hdr.data_size = (uint32_t)((size_t)w * h * 4);
            std::vector<uint8_t> pvr(sizeof(GamePvrHdr) + (size_t)w * h * 4, 0);
            memcpy(pvr.data(), &hdr, sizeof(GamePvrHdr));
            memcpy(pvr.data() + sizeof(GamePvrHdr), flipped.data(), (size_t)w * h * 4);
            tex = std::move(pvr);
            return true;
        }

        int bw = (w + 3) / 4, bh = (h + 3) / 4;
        size_t etc1_bytes = (size_t)bw * bh * 8;
        GamePvrHdr hdr;
        hdr.height    = (uint32_t)h;
        hdr.width     = (uint32_t)w;
        // flags/mip_count/mask already default to stock ETC1 values (0x10036 / 0 / FFFFFFFF).
        // Only data_size is per-texture.
        hdr.data_size = (uint32_t)etc1_bytes;
        std::vector<uint8_t> pvr(sizeof(GamePvrHdr) + etc1_bytes, 0);
        memcpy(pvr.data(), &hdr, sizeof(GamePvrHdr));

        int pw = bw * 4, ph = bh * 4;
        std::vector<uint8_t> padded((size_t)pw * ph * 4, 0);
        for (int y = 0; y < h; y++)
            memcpy(padded.data() + (size_t)y * pw * 4, flipped.data() + (size_t)y * w * 4, (size_t)w * 4);
        for (int y = 0; y < h; y++)
            for (int x = w; x < pw; x++)
                memcpy(padded.data() + ((size_t)y * pw + x) * 4, padded.data() + ((size_t)y * pw + (w - 1)) * 4, 4);
        for (int y = h; y < ph; y++)
            memcpy(padded.data() + (size_t)y * pw * 4, padded.data() + (size_t)(h - 1) * pw * 4, (size_t)pw * 4);

        uint8_t* out_blocks = pvr.data() + sizeof(GamePvrHdr);
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++) {
                const uint8_t* src = padded.data() + (size_t)(by * 4) * pw * 4 + (size_t)(bx * 4) * 4;
                encode_etc1_block(src, pw * 4, out_blocks + (size_t)(by * bw + bx) * 8);
            }
        tex = std::move(pvr);
        return true;
    }

    // Native TEX container: 12-byte header {type=1 RGBA8888, w, h} + payload,
    // gzipped. This is the loader format for .tex.png (see load_tex_png).
    std::vector<uint8_t> texraw(12 + (size_t)w * h * 4, 0);
    uint32_t hdr3[3] = { 1, (uint32_t)w, (uint32_t)h };
    memcpy(texraw.data(), hdr3, 12);
    memcpy(texraw.data() + 12, flipped.data(), (size_t)w * h * 4);
    if (!gzip_compress(texraw, tex)) {
        if (err) *err = "gzip compression of texture failed";
        return false;
    }
    return true;
}

// ─── Texture resolution (mirrors tools/fbx_import.cpp) ────────────────────
static std::string strip_ext(const std::string& in) {
    std::string base = in;
    size_t nul = base.find('\0');
    if (nul != std::string::npos) base.resize(nul);
    size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    return base;
}

static std::string find_source_image(const fs::path& fbx_dir, const std::string& tex_name,
                                     std::error_code& ec) {
    std::vector<fs::path> roots = { fbx_dir };
    if (fs::is_directory(fbx_dir, ec))
        for (const char* sub : {"images", "textures", "maps", "Texture", "Textures"}) {
            fs::path p = fbx_dir / sub;
            if (fs::is_directory(p, ec)) roots.push_back(p);
        }
    std::string stem = strip_ext(tex_name);
    static const char* exts[] = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
    for (const auto& root : roots) {
        for (const char* e : exts) {
            fs::path c = root / (stem + e);
            if (fs::is_regular_file(c, ec)) return c.string();
        }
        for (const char* e : exts) {
            fs::path c = root / (stem + ".tex" + e);
            if (fs::is_regular_file(c, ec)) return c.string();
        }
    }
    return {};
}

// ─── Smart texture naming & filtering helpers ───────────────────────────────
bool is_generic_texture_name(const std::string& name_or_stem, std::string* out_suffix) {
    std::string stem = strip_ext(name_or_stem);
    std::string low = stem;
    for (char& c : low) c = (char)tolower((unsigned char)c);

    const char* prefixes[] = { "texture", "image", "material", "mat" };
    for (const char* pfx : prefixes) {
        size_t plen = strlen(pfx);
        if (low.rfind(pfx, 0) == 0) { // starts with prefix
            std::string rem = stem.substr(plen);
            std::string rem_low = low.substr(plen);
            bool all_digits = true;
            for (char c : rem_low) {
                if (!isdigit((unsigned char)c) && c != '_' && c != '-') {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits) {
                if (out_suffix) *out_suffix = rem;
                return true;
            }
        }
    }
    return false;
}

bool is_non_diffuse_texture_name(const std::string& name_or_stem) {
    std::string stem = strip_ext(name_or_stem);
    std::string low = stem;
    for (char& c : low) c = (char)tolower((unsigned char)c);

    // Common non-diffuse suffixes
    static const char* kSuffixes[] = {
        "_n", "-n", "_norm", "-norm", "_normal", "-normal", "_normals", "-normals",
        "_nrm", "-nrm", "_rough", "-rough", "_roughness", "-roughness",
        "_metal", "-metal", "_metallic", "-metallic", "_metalness", "-metalness",
        "_ao", "-ao", "_occlusion", "-occlusion", "_ambientocclusion", "-ambientocclusion",
        "_bump", "-bump", "_height", "-height", "_spec", "-spec",
        "_specular", "-specular", "_disp", "-disp", "_displacement", "-displacement"
    };
    for (const char* suf : kSuffixes) {
        size_t slen = strlen(suf);
        if (low.size() >= slen && low.compare(low.size() - slen, slen, suf) == 0) {
            return true;
        }
    }

    // Token check for standalone words
    static const char* kKeywords[] = {
        "normal", "norm", "nrm", "roughness", "rough", "metallic", "metal",
        "metalness", "metalrough", "metallicroughness", "occlusion", "ao",
        "ambientocclusion", "specular", "spec", "gloss", "glossiness",
        "height", "bump", "disp", "displacement", "curvature"
    };
    for (const char* kw : kKeywords) {
        size_t pos = low.find(kw);
        if (pos != std::string::npos) {
            bool left_boundary = (pos == 0 || low[pos - 1] == '_' || low[pos - 1] == '-' || low[pos - 1] == ' ' || isdigit((unsigned char)low[pos - 1]));
            size_t end_pos = pos + strlen(kw);
            bool right_boundary = (end_pos == low.size() || low[end_pos] == '_' || low[end_pos] == '-' || low[end_pos] == ' ' || isdigit((unsigned char)low[end_pos]));
            if (left_boundary && right_boundary) {
                return true;
            }
        }
    }

    return false;
}

static std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> words;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '_' || c == '-' || c == ' ' || c == '.') {
            if (!cur.empty()) { words.push_back(cur); cur.clear(); }
        } else if (isupper((unsigned char)c) && !cur.empty() &&
                   (i + 1 < s.size() && islower((unsigned char)s[i + 1]))) {
            words.push_back(cur);
            cur.clear();
            cur += c;
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) words.push_back(cur);
    return words;
}

std::string get_model_prefix_first_5_words(const std::string& model_name_or_stem) {
    std::string stem = strip_ext(model_name_or_stem);
    auto words = split_words(stem);
    if (words.empty()) return "model";
    std::string prefix;
    size_t count = std::min<size_t>(words.size(), 5);
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) prefix += "_";
        prefix += words[i];
    }
    return prefix;
}

std::string resolve_output_texture_stem(const std::string& model_stem,
                                       const std::string& tex_stem,
                                       size_t tex_index,
                                       bool smart_naming) {
    if (!smart_naming) {
        return tex_stem;
    }
    std::string num_suffix;
    if (is_generic_texture_name(tex_stem, &num_suffix)) {
        std::string prefix = get_model_prefix_first_5_words(model_stem);
        if (num_suffix.empty()) {
            return prefix + std::to_string(tex_index);
        }
        return prefix + num_suffix;
    }
    return tex_stem;
}

static bool is_normal_map_pixels(const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;
    int samples = std::min(w * h, 1024);
    int step = std::max(1, (w * h) / samples);
    int normal_votes = 0;
    int total_tested = 0;

    for (int i = 0; i < w * h; i += step) {
        int r = rgba[i * 4 + 0];
        int g = rgba[i * 4 + 1];
        int b = rgba[i * 4 + 2];
        int a = rgba[i * 4 + 3];
        if (a < 10) continue;
        total_tested++;
        // Tangent space normal map check: dominant blue, R and G centered near 128
        if (b >= 160 && r >= 80 && r <= 175 && g >= 80 && g <= 175 &&
            (b - r) >= 25 && (b - g) >= 25) {
            normal_votes++;
        }
    }
    if (total_tested < 16) return false;
    return (normal_votes * 100 / total_tested) >= 65;
}

static bool is_dummy_flat_texture(const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;
    int samples = std::min(w * h, 256);
    int step = std::max(1, (w * h) / samples);
    int r0 = rgba[0], g0 = rgba[1], b0 = rgba[2];
    bool uniform = true;
    for (int i = 0; i < w * h; i += step) {
        int r = rgba[i * 4 + 0];
        int g = rgba[i * 4 + 1];
        int b = rgba[i * 4 + 2];
        if (std::abs(r - r0) > 4 || std::abs(g - g0) > 4 || std::abs(b - b0) > 4) {
            uniform = false;
            break;
        }
    }
    if (uniform && (w <= 32 && h <= 32)) return true;
    if (uniform && r0 >= 248 && g0 >= 248 && b0 >= 248) return true;
    return false;
}
// The glTF→POD path keeps the source scene's node hierarchy (Sketchfab_model
// root with unit-conversion scales, GLTF_SceneRootNode axis rotations, 500+
// skeleton bones, …). The game's PODLoader::CreateModel BAKES every mesh
// node's world matrix into its vertices at load time, so those root
// transforms silently rescale / rotate / even zero-out the geometry in-game
// while the ruby visualiser (which applies node matrices at render time)
// shows something else. Fix: bake every mesh node's world matrix into its
// vertex positions right here, collapse mesh nodes to identity, drop the
// non-mesh hierarchy for static models, and re-center the bounding box at
// the origin so scene-object placement is predictable.
static void bake_and_center_model(PODModel& model, bool center) {
    if (model.meshes.empty()) return;

    // 1) Bake each mesh node's world matrix into its vertex positions and
    //    normals, then set the node transform to identity.
    //
    // REGRESSION FIX (ccity_building_set_1.glb): the world matrix of EVERY
    // node MUST be captured up-front, BEFORE any node matrix is mutated.
    // A building set is a deep hierarchy (depth 8, 1357 mesh nodes among 3058
    // nodes) where a mesh node can itself be the PARENT of other mesh nodes.
    // The previous code baked and collapsed each mesh node to identity *inside*
    // the same loop; a child mesh processed later then read its already-wiped
    // ancestor's matrix as identity, so get_node_matrix() returned a partial
    // (wrong) world transform and the building landed at the wrong place. By
    // snapshotting all world matrices first, baking becomes order-independent.
    std::vector<std::array<float, 16>> world_of(model.nodes.size());
    for (int ni = 0; ni < (int)model.nodes.size(); ++ni) {
        av::get_node_matrix(model, ni, 0.0f, world_of[ni].data());
    }

    for (int ni = 0; ni < (int)model.nodes.size(); ++ni) {
        auto& node = model.nodes[ni];
        if (node.object_index < 0 ||
            node.object_index >= (int)model.meshes.size())
            continue;

        // REGRESSION FIX (minecraft_bee.glb / tung_tung_sahur.glb): a SKINNED
        // mesh must NEVER be world-baked. Its vertices live in skinning space and
        // are posed at render time by the bone palette (bind + current bone
        // matrices, which already fold in the skeleton's world transform via the
        // inverseBindMatrices). Baking the mesh node's world matrix into the
        // vertices here would apply that transform a SECOND time on top of
        // skinning, collapsing the model to a tiny/exploded blob. Only static
        // meshes (bones_per_vertex == 0) may be baked.
        if (model.meshes[node.object_index].bones_per_vertex > 0) continue;

        const float* world = world_of[ni].data();

        bool identity = true;
        for (int k = 0; k < 16; ++k) {
            float expect = (k == 0 || k == 5 || k == 10 || k == 15) ? 1.0f : 0.0f;
            if (std::fabs(world[k] - expect) > 1e-6f) { identity = false; break; }
        }
        if (identity) continue;   // nothing to bake

        auto& mesh = model.meshes[node.object_index];
        // Positions.
        for (size_t v = 0; v + 2 < mesh.positions.size(); v += 3) {
            float x = mesh.positions[v], y = mesh.positions[v+1], z = mesh.positions[v+2];
            mesh.positions[v]   = world[0]*x + world[4]*y + world[8]*z  + world[12];
            mesh.positions[v+1] = world[1]*x + world[5]*y + world[9]*z  + world[13];
            mesh.positions[v+2] = world[2]*x + world[6]*y + world[10]*z + world[14];
        }
        // Normals (rotation-only: use upper 3×3, no translation).
        for (size_t v = 0; v + 2 < mesh.normals.size(); v += 3) {
            float x = mesh.normals[v], y = mesh.normals[v+1], z = mesh.normals[v+2];
            float nx = world[0]*x + world[4]*y + world[8]*z;
            float ny = world[1]*x + world[5]*y + world[9]*z;
            float nz = world[2]*x + world[6]*y + world[10]*z;
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
            mesh.normals[v] = nx; mesh.normals[v+1] = ny; mesh.normals[v+2] = nz;
        }
        // Collapse the node to identity so the POD carries no hidden transform.
        for (int k = 0; k < 16; ++k) node.matrix[k] = 0.0f;
        node.matrix[0] = node.matrix[5] = node.matrix[10] = node.matrix[15] = 1.0f;
        node.has_matrix = true;
        node.has_translation = node.has_rotation = node.has_scale = false;
    }

    // 2) Drop non-mesh hierarchy nodes (skeleton bones, glTF root wrappers).
    //    For static models they only poison the bbox and confuse the editor.
    //    Keep the nodes the game needs: mesh nodes first, then any named
    //    CenterPoint. Bone-animated models keep their skeleton.
    bool has_bones = false;
    for (const auto& m : model.meshes)
        if (m.bones_per_vertex > 0) { has_bones = true; break; }

    if (!has_bones) {
        std::vector<PODNode> kept;
        kept.reserve(model.nodes.size());
        for (auto& n : model.nodes) {
            bool is_mesh = n.object_index >= 0 &&
                           n.object_index < (int)model.meshes.size() &&
                           n.object_index < model.num_mesh_nodes;
            bool is_center = (n.name == "CenterPoint");
            if (is_mesh || is_center) {
                n.parent_index = -1;   // flat hierarchy after the strip
                kept.push_back(std::move(n));
            }
        }
        model.nodes = std::move(kept);
        model.num_mesh_nodes = (int)model.nodes.size();
    }

    // 3) Center the bounding box at the origin (predictable scene placement).
    //    Recompute mesh AABBs from the BAKED positions — the loader's cached
    //    AABBs are pre-bake and stale after step 1.
    //    Skip entirely for skinned models: their vertices are in skinning space
    //    and must stay aligned to the (un-centered) skeleton, or the bone
    //    palette poses them away from the shifted geometry.
    if (center && !has_bones) {
        float mnx =  1e30f, mny =  1e30f, mnz =  1e30f;
        float mxx = -1e30f, mxy = -1e30f, mxz = -1e30f;
        for (auto& m : model.meshes) {
            m.min_x = m.min_y = m.min_z =  1e9f;
            m.max_x = m.max_y = m.max_z = -1e9f;
            for (size_t v = 0; v + 2 < m.positions.size(); v += 3) {
                float px = m.positions[v], py = m.positions[v+1], pz = m.positions[v+2];
                if (px < m.min_x) m.min_x = px; if (px > m.max_x) m.max_x = px;
                if (py < m.min_y) m.min_y = py; if (py > m.max_y) m.max_y = py;
                if (pz < m.min_z) m.min_z = pz; if (pz > m.max_z) m.max_z = pz;
            }
            if (m.positions.empty()) continue;
            mnx = std::min(mnx, m.min_x); mxx = std::max(mxx, m.max_x);
            mny = std::min(mny, m.min_y); mxy = std::max(mxy, m.max_y);
            mnz = std::min(mnz, m.min_z); mxz = std::max(mxz, m.max_z);
        }
        if (mnx <= mxx) {
            float cx = (mnx + mxx) * 0.5f;
            float cy = (mny + mxy) * 0.5f;
            float cz = (mnz + mxz) * 0.5f;
            for (auto& m : model.meshes) {
                for (size_t v = 0; v + 2 < m.positions.size(); v += 3) {
                    m.positions[v]   -= cx;
                    m.positions[v+1] -= cy;
                    m.positions[v+2] -= cz;
                }
                m.min_x -= cx; m.max_x -= cx;
                m.min_y -= cy; m.max_y -= cy;
                m.min_z -= cz; m.max_z -= cz;
            }
        }
    }
}

bool fbx_to_pod(const std::string& fbx_path, const std::string& pod_path,
                const PodConvertOptions& opts,
                std::vector<std::string>* written_textures,
                std::string* err) {
    PODModel model = fbx_load(fbx_path);

    if (model.meshes.empty()) {
        if (err) *err = "no meshes found (unsupported FBX or parse failure)";
        return false;
    }

    // FBX/glTF UVs put v = 0 at the top of the texture; the game samples POD
    // UVs with v = 0 at the bottom, so flip when carrying FBX previews into a
    // game asset. EXCEPTION: ufbx already flips V when it mirrors a left-handed
    // FBX scene (3ds Max / Unity exports) — model.uv_v_flipped is true then, and
    // flipping again would mirror textures upside-down. Mirror the viewer's dcc_uv
    // logic (asset_viewer.cpp).
    if (opts.flip_v && !model.uv_v_flipped) {
        for (auto& m : model.meshes) {
            for (size_t i = 0; i + 1 < m.uvs.size(); i += 2)
                m.uvs[i + 1] = 1.0f - m.uvs[i + 1];
        }
    }

    // Scale positions. FBX authored in centimetres is silently divided by 100 by ufbx
    // (target_unit_meters=1.0), so `opts.unit_scale` lets the user undo that and reach
    // game units (≈ 39.4 units / metre). Combined with the class-level `opts.scale`,
    // effective multiplier = unit_scale * scale.
    float fbx_scale = opts.unit_scale * opts.scale;
    if (fbx_scale != 1.0f && fbx_scale > 0.0f) {
        for (auto& m : model.meshes) {
            for (float& pv : m.positions) pv *= fbx_scale;
        }
    }

    if (opts.convert_textures) {
        fs::path fbx_dir = fs::path(fbx_path).parent_path();
        fs::path pod_dir = fs::path(pod_path).parent_path();
        if (pod_dir.empty()) pod_dir = ".";
        std::string pod_stem = strip_ext(fs::path(pod_path).filename().string());
        std::error_code ec;
        for (size_t i = 0; i < model.texture_filenames.size(); ++i) {
            std::string& tex_name = model.texture_filenames[i];
            if (tex_name.empty()) continue;
            std::string stem = strip_ext(tex_name);

            // Filter non-diffuse maps by name
            if (opts.filter_non_diffuse && is_non_diffuse_texture_name(stem)) {
                fprintf(stderr, "  ! texture '%s' filtered out (non-diffuse map)\n", tex_name.c_str());
                for (auto& mat : model.materials) {
                    if (mat.diffuse_texture_index == (int)i) mat.diffuse_texture_index = -1;
                }
                tex_name.clear();
                continue;
            }

            std::string src = find_source_image(fbx_dir, tex_name, ec);
            if (src.empty()) {
                fprintf(stderr, "  ! texture '%s' not found next to the FBX — skipped\n", tex_name.c_str());
                tex_name.clear();
                continue;
            }
            int w = 0, h = 0, comp = 0;
            uint8_t* rgba = stbi_load(src.c_str(), &w, &h, &comp, 4);
            if (!rgba) {
                fprintf(stderr, "  ! texture '%s' failed to decode — skipped\n", src.c_str());
                tex_name.clear();
                continue;
            }

            // Pixel-based inspection for non-diffuse maps (purple normal maps, white dummy textures)
            if (opts.filter_non_diffuse) {
                if (is_normal_map_pixels(rgba, w, h)) {
                    fprintf(stderr, "  ! texture '%s' (%dx%d) detected as normal map (purple) — filtered out for Swordigo\n",
                            tex_name.c_str(), w, h);
                    stbi_image_free(rgba);
                    for (auto& mat : model.materials) {
                        if (mat.diffuse_texture_index == (int)i) mat.diffuse_texture_index = -1;
                    }
                    tex_name.clear();
                    continue;
                }
                if (is_dummy_flat_texture(rgba, w, h)) {
                    fprintf(stderr, "  ! texture '%s' (%dx%d) detected as flat/dummy non-diffuse texture — filtered out\n",
                            tex_name.c_str(), w, h);
                    stbi_image_free(rgba);
                    for (auto& mat : model.materials) {
                        if (mat.diffuse_texture_index == (int)i) mat.diffuse_texture_index = -1;
                    }
                    tex_name.clear();
                    continue;
                }
            }

            std::vector<uint8_t> tex;
            bool ok = encode_texture(rgba, w, h, opts.output_pvr, opts.pvr_resolution, tex, err);
            stbi_image_free(rgba);
            if (!ok) return false;

            std::string out_stem = resolve_output_texture_stem(pod_stem, stem, i, opts.smart_texture_naming);
            std::string out_name = out_stem + (opts.output_pvr ? ".pvr" : ".tex.png");
            std::string out_path = (pod_dir / out_name).string();
            if (!opts.overwrite && fs::exists(out_path)) {
                fprintf(stderr, "  ! %s already exists (use --force to overwrite)\n", out_name.c_str());
            } else if (!write_file(out_path, tex.data(), tex.size())) {
                if (err) *err = "cannot write texture file: " + out_path;
                return false;
            } else {
                fprintf(stderr, "  ✓ texture %s  (%dx%d → %.1f kB)\n", out_name.c_str(), w, h,
                        tex.size() / 1024.0);
            }
            tex_name = out_name; // POD references the file we just wrote
            if (written_textures) written_textures->push_back(out_name);
        }

        // Compact textures list and update material indices
        std::vector<std::string> compacted;
        std::vector<int> remap(model.texture_filenames.size(), -1);
        for (size_t i = 0; i < model.texture_filenames.size(); ++i) {
            if (!model.texture_filenames[i].empty()) {
                remap[i] = (int)compacted.size();
                compacted.push_back(model.texture_filenames[i]);
            }
        }
        for (auto& mat : model.materials) {
            if (mat.diffuse_texture_index >= 0 && mat.diffuse_texture_index < (int)remap.size()) {
                mat.diffuse_texture_index = remap[mat.diffuse_texture_index];
            } else {
                mat.diffuse_texture_index = -1;
            }
        }
        model.texture_filenames = std::move(compacted);
    }

    if (!opts.overwrite && fs::exists(pod_path)) {
        if (err) *err = "output exists (use --force to overwrite): " + pod_path;
        return false;
    }
    // Bake FBX node transforms into vertices and center the model. The FBX
    // importer already bakes geometry_to_world, so the bake is usually a no-op,
    // but centering gives predictable scene placement.
    bake_and_center_model(model, /*center=*/true);
    if (!pod_write(model, pod_path, err)) {
        if (err && err->empty()) *err = "POD serialization failed";
        return false;
    }
    return true;
}

bool glb_to_pod(const std::string& glb_path,
                const std::string& pod_path,
                const PodConvertOptions& opts,
                std::vector<std::string>* written_textures,
                std::vector<std::string>* written_clips,
                std::string* err) {
    PODModel model;
    std::vector<GLTFImageBuffer> embedded_images;
    GLTFPBRInfo pbr;
    bool is_glb = (glb_path.size() >= 4 && glb_path.compare(glb_path.size() - 4, 4, ".glb") == 0);
    bool ok = is_glb ? gltf_import_glb(glb_path, model, embedded_images, err, &pbr, opts.scale, opts.rigid_skin)
                     : gltf_import_gltf(glb_path, model, embedded_images, err, &pbr, opts.scale, opts.rigid_skin);
    if (!ok) return false;

    if (opts.flip_v) {
        for (auto& m : model.meshes) {
            for (size_t i = 0; i + 1 < m.uvs.size(); i += 2)
                m.uvs[i + 1] = 1.0f - m.uvs[i + 1];
        }
    }

    fs::path pod_dir = fs::path(pod_path).parent_path();
    if (pod_dir.empty()) pod_dir = ".";
    std::string pod_stem = strip_ext(fs::path(pod_path).filename().string());

    if (opts.convert_textures) {
        fs::path glb_dir = fs::path(glb_path).parent_path();
        std::error_code ec;

        // Map each diffuse texture slot to its exact glTF image index in embedded_images
        std::vector<int> tex_to_img(model.texture_filenames.size(), -1);
        for (size_t mi = 0; mi < model.materials.size() && mi < pbr.materials.size(); ++mi) {
            int ti = model.materials[mi].diffuse_texture_index;
            if (ti >= 0 && ti < (int)tex_to_img.size()) {
                if (tex_to_img[ti] < 0 && pbr.materials[mi].base_tex >= 0) {
                    tex_to_img[ti] = pbr.materials[mi].base_tex;
                }
            }
        }

        for (size_t i = 0; i < model.texture_filenames.size(); ++i) {
            std::string& tex_name = model.texture_filenames[i];
            if (tex_name.empty()) continue;
            std::string stem = strip_ext(tex_name);

            // Filter non-diffuse textures by name
            if (opts.filter_non_diffuse && is_non_diffuse_texture_name(stem)) {
                fprintf(stderr, "  ! texture '%s' filtered out (non-diffuse map)\n", tex_name.c_str());
                for (auto& mat : model.materials) {
                    if (mat.diffuse_texture_index == (int)i) mat.diffuse_texture_index = -1;
                }
                tex_name.clear();
                continue;
            }

            int w = 0, h = 0, comp = 0;
            uint8_t* rgba = nullptr;

            // Check if we have an embedded image payload for this texture slot via tex_to_img
            int img_idx = (i < tex_to_img.size()) ? tex_to_img[i] : -1;
            if (img_idx >= 0 && img_idx < (int)embedded_images.size() && !embedded_images[img_idx].data.empty()) {
                rgba = stbi_load_from_memory(embedded_images[img_idx].data.data(),
                                             (int)embedded_images[img_idx].data.size(),
                                             &w, &h, &comp, 4);
            } else if (i < embedded_images.size() && !embedded_images[i].data.empty()) {
                // Fallback to slot i if tex_to_img wasn't resolved
                rgba = stbi_load_from_memory(embedded_images[i].data.data(),
                                             (int)embedded_images[i].data.size(),
                                             &w, &h, &comp, 4);
            }

            // Fallback: look on disk
            if (!rgba) {
                std::string src = find_source_image(glb_dir, tex_name, ec);
                if (!src.empty()) {
                    rgba = stbi_load(src.c_str(), &w, &h, &comp, 4);
                }
            }

            if (!rgba) {
                fprintf(stderr, "  ! texture '%s' could not be loaded — skipped\n", tex_name.c_str());
                tex_name.clear();
                continue;
            }

            // Pixel-based inspection for non-diffuse maps (purple normal maps, white dummy textures)
            if (opts.filter_non_diffuse) {
                if (is_normal_map_pixels(rgba, w, h)) {
                    fprintf(stderr, "  ! texture '%s' (%dx%d) detected as normal map (purple) — filtered out for Swordigo\n",
                            tex_name.c_str(), w, h);
                    stbi_image_free(rgba);
                    for (auto& mat : model.materials) {
                        if (mat.diffuse_texture_index == (int)i) mat.diffuse_texture_index = -1;
                    }
                    tex_name.clear();
                    continue;
                }
                if (is_dummy_flat_texture(rgba, w, h)) {
                    fprintf(stderr, "  ! texture '%s' (%dx%d) detected as flat/dummy non-diffuse texture — filtered out\n",
                            tex_name.c_str(), w, h);
                    stbi_image_free(rgba);
                    for (auto& mat : model.materials) {
                        if (mat.diffuse_texture_index == (int)i) mat.diffuse_texture_index = -1;
                    }
                    tex_name.clear();
                    continue;
                }
            }

            std::vector<uint8_t> tex;
            bool enc_ok = encode_texture(rgba, w, h, opts.output_pvr, opts.pvr_resolution, tex, err);
            stbi_image_free(rgba);
            if (!enc_ok) return false;

            std::string out_stem = resolve_output_texture_stem(pod_stem, stem, i, opts.smart_texture_naming);
            std::string out_name = out_stem + (opts.output_pvr ? ".pvr" : ".tex.png");
            std::string out_path = (pod_dir / out_name).string();
            if (!opts.overwrite && fs::exists(out_path)) {
                fprintf(stderr, "  ! %s already exists (use --force to overwrite)\n", out_name.c_str());
            } else if (!write_file(out_path, tex.data(), tex.size())) {
                if (err) *err = "cannot write texture file: " + out_path;
                return false;
            } else {
                fprintf(stderr, "  ✓ texture %s  (%dx%d → %.1f kB)\n", out_name.c_str(), w, h,
                        tex.size() / 1024.0);
            }
            tex_name = out_name;
            if (written_textures) written_textures->push_back(out_name);
        }

        // Compact textures list and update material indices
        std::vector<std::string> compacted;
        std::vector<int> remap(model.texture_filenames.size(), -1);
        for (size_t i = 0; i < model.texture_filenames.size(); ++i) {
            if (!model.texture_filenames[i].empty()) {
                remap[i] = (int)compacted.size();
                compacted.push_back(model.texture_filenames[i]);
            }
        }
        for (auto& mat : model.materials) {
            if (mat.diffuse_texture_index >= 0 && mat.diffuse_texture_index < (int)remap.size()) {
                mat.diffuse_texture_index = remap[mat.diffuse_texture_index];
            } else {
                mat.diffuse_texture_index = -1;
            }
        }
        model.texture_filenames = std::move(compacted);
    }

    if (!opts.overwrite && fs::exists(pod_path)) {
        if (err) *err = "output exists (use --force to overwrite): " + pod_path;
        return false;
    }
    // Bake glTF node transforms into vertices and center the model. CRITICAL:
    // glTF scenes carry root-node unit-conversion scales (Sketchfab 394×) and
    // axis-rotation matrices (GLTF_SceneRootNode Z-up→Y-up). The game's
    // PODLoader bakes these into vertices at load, producing unpredictable
    // in-game sizes. Baking here + collapsing to identity makes the POD
    // self-contained: what you see in the converter output IS what the game
    // renders, and the bbox/center/feet computations are all correct.
    bake_and_center_model(model, /*center=*/true);
    if (!pod_write(model, pod_path, err)) {
        if (err && err->empty()) *err = "POD serialization failed";
        return false;
    }

    // Extract separate animation clip POD files (<pod_stem>_<clip_name>.pod)
    std::vector<std::pair<std::string, PODModel>> clips;
    if (gltf_import_all_clips(glb_path, clips, nullptr, opts.scale, opts.anim_fps, opts.rigid_skin)) {
        for (const auto& kv : clips) {
            std::string clip_pod_name = pod_stem + "_" + kv.first + ".POD";
            std::string clip_pod_path = (pod_dir / clip_pod_name).string();
            if (opts.overwrite || !fs::exists(clip_pod_path)) {
                std::string clip_err;
                if (pod_write(kv.second, clip_pod_path, &clip_err)) {
                    fprintf(stderr, "  ✓ clip POD: %s (%d frames @ %.0f fps)\n",
                            clip_pod_name.c_str(), kv.second.num_frames, kv.second.fps);
                    if (written_clips) written_clips->push_back(clip_pod_name);
                }
            }
        }
    }

    return true;
}

// ─── CLI ───────────────────────────────────────────────────────────────────
// ─── OBJ → POD (E17) ─────────────────────────────────────────────────────
// Static geometry only: OBJ has no rigs or animation. Reuses the viewer's
// tolerant obj_load (v/vt/vn, quads/ngons fan-triangulated, optional .mtl
// map_Kd) and runs the same bake/center + texture re-encode as FBX/GLB.
bool obj_to_pod(const std::string& obj_path,
                const std::string& pod_path,
                const PodConvertOptions& opts,
                std::vector<std::string>* written_textures,
                std::string* err) {
    PODModel model;
    if (!obj_load(obj_path, model, err)) {
        if (err && err->empty()) *err = "OBJ parse failure";
        return false;
    }
    if (model.meshes.empty()) {
        if (err) *err = "no meshes found in OBJ";
        return false;
    }

    // OBJ UVs are DCC top-origin, identical to FBX/glTF: flip to the game's
    // bottom-origin convention unless the user opts out with --no-flip.
    if (opts.flip_v && !model.uv_v_flipped) {
        for (auto& m : model.meshes) {
            for (size_t i = 0; i + 1 < m.uvs.size(); i += 2)
                m.uvs[i + 1] = 1.0f - m.uvs[i + 1];
        }
        model.uv_v_flipped = true;
    }

    // Uniform scale (same semantics as FBX: unit_scale * scale).
    float obj_scale = opts.unit_scale * opts.scale;
    if (obj_scale != 1.0f && obj_scale > 0.0f) {
        for (auto& m : model.meshes) {
            for (float& pv : m.positions) pv *= obj_scale;
        }
    }

    if (opts.convert_textures) {
        fs::path obj_dir = fs::path(obj_path).parent_path();
        fs::path pod_dir = fs::path(pod_path).parent_path();
        if (pod_dir.empty()) pod_dir = ".";
        std::string pod_stem = strip_ext(fs::path(pod_path).filename().string());
        std::error_code ec;
        for (size_t i = 0; i < model.texture_filenames.size(); ++i) {
            std::string& tex_name = model.texture_filenames[i];
            if (tex_name.empty()) continue;
            std::string stem = strip_ext(tex_name);

            // Filter non-diffuse maps by name
            if (opts.filter_non_diffuse && is_non_diffuse_texture_name(stem)) {
                fprintf(stderr, "  ! texture '%s' filtered out (non-diffuse map)\n", tex_name.c_str());
                for (auto& mat : model.materials) {
                    if (mat.diffuse_texture_index == (int)i) mat.diffuse_texture_index = -1;
                }
                tex_name.clear();
                continue;
            }

            std::string src = find_source_image(obj_dir, tex_name, ec);
            if (src.empty()) {
                fprintf(stderr, "  ! texture '%s' not found next to the OBJ — skipped\n", tex_name.c_str());
                tex_name.clear();
                continue;
            }
            int w = 0, h = 0, comp = 0;
            uint8_t* rgba = stbi_load(src.c_str(), &w, &h, &comp, 4);
            if (!rgba) {
                fprintf(stderr, "  ! texture '%s' failed to decode — skipped\n", src.c_str());
                tex_name.clear();
                continue;
            }

            // Pixel-based inspection for non-diffuse maps (purple normal maps, white dummy textures)
            if (opts.filter_non_diffuse) {
                if (is_normal_map_pixels(rgba, w, h)) {
                    fprintf(stderr, "  ! texture '%s' (%dx%d) detected as normal map (purple) — filtered out for Swordigo\n",
                            tex_name.c_str(), w, h);
                    stbi_image_free(rgba);
                    for (auto& mat : model.materials) {
                        if (mat.diffuse_texture_index == (int)i) mat.diffuse_texture_index = -1;
                    }
                    tex_name.clear();
                    continue;
                }
                if (is_dummy_flat_texture(rgba, w, h)) {
                    fprintf(stderr, "  ! texture '%s' (%dx%d) detected as flat/dummy non-diffuse texture — filtered out\n",
                            tex_name.c_str(), w, h);
                    stbi_image_free(rgba);
                    for (auto& mat : model.materials) {
                        if (mat.diffuse_texture_index == (int)i) mat.diffuse_texture_index = -1;
                    }
                    tex_name.clear();
                    continue;
                }
            }

            std::vector<uint8_t> tex;
            bool ok = encode_texture(rgba, w, h, opts.output_pvr, opts.pvr_resolution, tex, err);
            stbi_image_free(rgba);
            if (!ok) return false;

            std::string out_stem = resolve_output_texture_stem(pod_stem, stem, i, opts.smart_texture_naming);
            std::string out_name = out_stem + (opts.output_pvr ? ".pvr" : ".tex.png");
            std::string out_path = (pod_dir / out_name).string();
            if (!opts.overwrite && fs::exists(out_path)) {
                fprintf(stderr, "  ! %s already exists (use --force to overwrite)\n", out_name.c_str());
            } else if (!write_file(out_path, tex.data(), tex.size())) {
                if (err) *err = "cannot write texture file: " + out_path;
                return false;
            } else {
                fprintf(stderr, "  ✓ texture %s  (%dx%d → %.1f kB)\n", out_name.c_str(), w, h,
                        tex.size() / 1024.0);
            }
            tex_name = out_name; // POD references the file we just wrote
            if (written_textures) written_textures->push_back(out_name);
        }

        // Compact textures list and update material indices
        std::vector<std::string> compacted;
        std::vector<int> remap(model.texture_filenames.size(), -1);
        for (size_t i = 0; i < model.texture_filenames.size(); ++i) {
            if (!model.texture_filenames[i].empty()) {
                remap[i] = (int)compacted.size();
                compacted.push_back(model.texture_filenames[i]);
            }
        }
        for (auto& mat : model.materials) {
            if (mat.diffuse_texture_index >= 0 && mat.diffuse_texture_index < (int)remap.size()) {
                mat.diffuse_texture_index = remap[mat.diffuse_texture_index];
            } else {
                mat.diffuse_texture_index = -1;
            }
        }
        model.texture_filenames = std::move(compacted);
    }

    if (!opts.overwrite && fs::exists(pod_path)) {
        if (err) *err = "output exists (use --force to overwrite): " + pod_path;
        return false;
    }
    // OBJ vertices are already baked (no node transforms in the format);
    // centering gives predictable scene placement, matching FBX/GLB.
    bake_and_center_model(model, /*center=*/true);
    if (!pod_write(model, pod_path, err)) {
        if (err && err->empty()) *err = "POD serialization failed";
        return false;
    }
    return true;
}

// ─── CLI ───────────────────────────────────────────────────────────────────
int pod_convert_cli(int argc, char** argv) {
    PodConvertOptions opts;
    std::string in_path, pod_path;
    bool is_glb = false;
    bool is_obj = false;

    for (int i = 0; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-flip") opts.flip_v = false;
        else if (a == "--no-textures") opts.convert_textures = false;
        else if (a == "--tex-png") opts.output_pvr = false;
        else if (a == "--no-filter-normals") opts.filter_non_diffuse = false;
        else if (a == "--no-smart-names") opts.smart_texture_naming = false;
        else if (a == "--smooth-skin") opts.rigid_skin = false;
        else if (a == "--anim-fps" && i + 1 < argc) {
            // Animation clip resample rate. The game hardcodes 24 fps in
            // CreateAnimationFromFile; 0 opts out into key-density derivation.
            opts.anim_fps = std::stof(argv[++i]);
            if (opts.anim_fps < 0.0f) opts.anim_fps = 0.0f;
        }
        else if (a == "--force" || a == "-f") opts.overwrite = true;
        else if ((a == "--scale" || a == "-s") && i + 1 < argc) {
            opts.scale = std::stof(argv[++i]);
        }
        else if ((a == "--unit" || a == "-u") && i + 1 < argc) {
            // FBX unit multiplier. ufbx converts to metres at load, so use this to
            // reach game units. Common: 1.0 (metres), 100.0 (centimetres), 39.37 (metres→inches).
            opts.unit_scale = std::stof(argv[++i]);
        }
        else if (a == "--help" || a == "-h") {
            printf("Usage: bin/ruby --fbx2pod <in.fbx> [out.pod] [options]\n"
                   "       bin/ruby --glb2pod <in.glb/gltf> [out.pod] [options]\n"
                   "       bin/ruby --obj2pod <in.obj> [out.pod] [options]\n"
                   "  out.pod        defaults to <in>.POD next to the source model\n"
                   "  --scale, -s    uniform scale factor applied to model (e.g. 80.0)\n"
                   "  --unit, -u     FBX unit multiplier (e.g. 100.0 for cm sources, 39.37 for m→inches)\n"
                   "  --no-flip      keep source V coordinates (skip bottom-origin flip)\n"
                   "  --no-textures  do not convert referenced textures\n"
                   "  --tex-png      emit gzipped native .tex.png (backgrounds)\n"
                   "                   instead of the default raw .pvr (game model textures)\n"
                   "  --no-filter-normals keep normal maps / PBR maps (default filters them out)\n"
                   "  --no-smart-names    keep generic texture names like texture0 (default renames)\n"
                   "  --anim-fps <r> animation clip resample rate (default 24 = game engine;\n"
                   "                   0 derives the rate from source key density)\n"
                   "  --smooth-skin  keep full multi-bone weights on glTF import (default\n"
                   "                   bakes each vertex to its dominant bone — game parity)\n"
                   "  --force, -f    overwrite existing output files\n");
            return 0;
        }
        else if (in_path.empty()) {
            in_path = a;
            std::string low = in_path;
            for (char& c : low) c = (char)tolower((unsigned char)c);
            if (low.rfind(".glb") != std::string::npos || low.rfind(".gltf") != std::string::npos)
                is_glb = true;
            if (low.size() >= 4 && low.compare(low.size() - 4, 4, ".obj") == 0)
                is_obj = true;
        }
        else if (pod_path.empty()) pod_path = a;
        else {
            fprintf(stderr, "unexpected argument: %s\n", a.c_str());
            return 1;
        }
    }
    if (in_path.empty()) {
        fprintf(stderr, "usage: bin/ruby --fbx2pod <in.fbx> [out.pod] [options]\n"
                        "       bin/ruby --glb2pod <in.glb> [out.pod] [options]\n"
                        "       bin/ruby --obj2pod <in.obj> [out.pod] [options]\n");
        return 1;
    }
    if (pod_path.empty())
        pod_path = (fs::path(in_path).parent_path() /
                    (strip_ext(fs::path(in_path).filename().string()) + ".POD")).string();

    std::vector<std::string> written_tex, written_clips;
    std::string err;
    printf("Converting %s → %s ...\n", in_path.c_str(), pod_path.c_str());
    bool ok = false;
    if (is_glb) {
        ok = glb_to_pod(in_path, pod_path, opts, &written_tex, &written_clips, &err);
    } else if (is_obj) {
        ok = obj_to_pod(in_path, pod_path, opts, &written_tex, &err);
    } else {
        ok = fbx_to_pod(in_path, pod_path, opts, &written_tex, &err);
    }

    if (!ok) {
        fprintf(stderr, "Model → POD failed: %s\n", err.empty() ? "unknown error" : err.c_str());
        return 1;
    }
    printf("  ✓ wrote %s (%zu textures, %zu animation clips)\n",
           pod_path.c_str(), written_tex.size(), written_clips.size());
    return 0;
}

} // namespace av