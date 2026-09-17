// ============================================================================
// boulderx.cpp — Zenith Mesh (BoulderX) Procedural Geometry Engine
//   Implementation of Ear-Clipping polygon triangulation, 3D variable-Z
//   procedural ground mesh generation, smooth trigonometric grass crests,
//   Protobuf byte serialization, and MeshPen .rbc container.
// ============================================================================

#include "tools/boulderx.h"
#include "platform/protobuf_reader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <numeric>

namespace zenith {

namespace {

constexpr double kPi = 3.14159265358979323846;

// ─── 2D Geometric Primitives ───────────────────────────────────────────────

struct Vec2 {
    double x, y;
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
};

inline double dot2(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline double cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline double length2(const Vec2& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
inline Vec2 normalize2(const Vec2& v) {
    double l = length2(v);
    return (l > 1e-9) ? Vec2{v.x / l, v.y / l} : Vec2{0.0, 0.0};
}

// ─── 3D Vector Math ────────────────────────────────────────────────────────

struct Vec3 {
    double x, y, z;
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
};

inline Vec3 cross3(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline double length3(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
inline Vec3 normalize3(const Vec3& v) {
    double l = length3(v);
    return (l > 1e-9) ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{0.0, 0.0, 1.0};
}

// Check if point P is inside triangle ABC (2D)
bool point_in_triangle_2d(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
    double cp1 = cross2(b - a, p - a);
    double cp2 = cross2(c - b, p - b);
    double cp3 = cross2(a - c, p - c);
    return (cp1 >= -1e-7 && cp2 >= -1e-7 && cp3 >= -1e-7) ||
           (cp1 <= 1e-7 && cp2 <= 1e-7 && cp3 <= 1e-7);
}

// Polygon signed area (positive = CCW, negative = CW)
double polygon_signed_area_2d(const std::vector<std::pair<double, double>>& pts) {
    double area = 0.0;
    size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        area += pts[i].first * pts[j].second - pts[j].first * pts[i].second;
    }
    return area * 0.5;
}

// Protobuf wire helpers
void encode_varint(std::vector<uint8_t>& buf, uint64_t val) {
    while (val >= 0x80) {
        buf.push_back((uint8_t)(val | 0x80));
        val >>= 7;
    }
    buf.push_back((uint8_t)val);
}

void encode_tag(std::vector<uint8_t>& buf, uint32_t field_number, uint8_t wire_type) {
    encode_varint(buf, (field_number << 3) | wire_type);
}

void encode_bytes(std::vector<uint8_t>& buf, uint32_t field_number, const uint8_t* data, size_t len) {
    encode_tag(buf, field_number, 2); // Length-delimited
    encode_varint(buf, len);
    buf.insert(buf.end(), data, data + len);
}

void encode_string(std::vector<uint8_t>& buf, uint32_t field_number, const std::string& str) {
    encode_bytes(buf, field_number, (const uint8_t*)str.data(), str.size());
}

} // anonymous namespace

// ─── ZenithGeneratedMesh metrics ───────────────────────────────────────────

size_t ZenithGeneratedMesh::total_vertices() const {
    size_t total = 0;
    for (const auto& sm : submeshes) total += sm.vertices.size();
    return total;
}

size_t ZenithGeneratedMesh::total_triangles() const {
    size_t total = 0;
    for (const auto& sm : submeshes) total += sm.indices.size() / 3;
    return total;
}

// ─── Industrial Ear-Clipping Triangulator ───────────────────────────────────

bool triangulate_polygon(const std::vector<std::pair<double, double>>& in_pts,
                         std::vector<uint16_t>& out_indices) {
    out_indices.clear();
    if (in_pts.size() < 3) return false;
    if (in_pts.size() > 65530) return false; // 16-bit index limit

    std::vector<Vec2> pts;
    pts.reserve(in_pts.size());
    for (const auto& p : in_pts) pts.push_back({p.first, p.second});

    // Enforce CCW winding
    double area = polygon_signed_area_2d(in_pts);
    bool reversed = false;
    if (area < 0.0) {
        std::reverse(pts.begin(), pts.end());
        reversed = true;
    }

    size_t n = pts.size();
    std::vector<size_t> v_indices(n);
    std::iota(v_indices.begin(), v_indices.end(), 0);

    size_t count = n;
    int max_guard = (int)(n * n * 2);
    int guard = 0;

    while (count > 2 && guard++ < max_guard) {
        bool ear_found = false;
        for (size_t i = 0; i < count; ++i) {
            size_t prev_idx = (i + count - 1) % count;
            size_t curr_idx = i;
            size_t next_idx = (i + 1) % count;

            const Vec2& a = pts[v_indices[prev_idx]];
            const Vec2& b = pts[v_indices[curr_idx]];
            const Vec2& c = pts[v_indices[next_idx]];

            // Must be convex corner
            if (cross2(b - a, c - b) <= 1e-9) continue;

            // Check if any other point is inside triangle ABC
            bool has_point_inside = false;
            for (size_t p = 0; p < count; ++p) {
                if (p == prev_idx || p == curr_idx || p == next_idx) continue;
                if (point_in_triangle_2d(pts[v_indices[p]], a, b, c)) {
                    has_point_inside = true;
                    break;
                }
            }

            if (!has_point_inside) {
                // Cut the ear!
                size_t orig_a = v_indices[prev_idx];
                size_t orig_b = v_indices[curr_idx];
                size_t orig_c = v_indices[next_idx];

                if (reversed) {
                    orig_a = (n - 1) - orig_a;
                    orig_b = (n - 1) - orig_b;
                    orig_c = (n - 1) - orig_c;
                    out_indices.push_back((uint16_t)orig_a);
                    out_indices.push_back((uint16_t)orig_c);
                    out_indices.push_back((uint16_t)orig_b);
                } else {
                    out_indices.push_back((uint16_t)orig_a);
                    out_indices.push_back((uint16_t)orig_b);
                    out_indices.push_back((uint16_t)orig_c);
                }

                v_indices.erase(v_indices.begin() + curr_idx);
                count--;
                ear_found = true;
                break;
            }
        }
        if (!ear_found) break; // Complex degenerate polygon fallback
    }

    return (out_indices.size() >= 3);
}

// ─── Procedural Zenith Mesh Synthesis ──────────────────────────────────────

bool generate_zenith_mesh(const ZenithMeshConfig& config, ZenithGeneratedMesh& out_mesh) {
    out_mesh.submeshes.clear();
    const size_t n = config.polygon.size();
    if (n < 3) return false;

    // 1. Prepare 2D points for triangulation
    std::vector<std::pair<double, double>> pts2d;
    pts2d.reserve(n);
    for (const auto& node : config.polygon) {
        pts2d.push_back({node.x, node.y});
    }

    // ── Submesh 1: Front Face (Concave Ear-Clipped with True 3D Z and Normals) ──
    if (config.generate_front) {
        ZenithSubmesh front;
        front.name = "FrontFace";
        front.texture_name = config.front_texture;

        std::vector<uint16_t> ear_indices;
        if (triangulate_polygon(pts2d, ear_indices)) {
            front.vertices.resize(n);
            front.indices = std::move(ear_indices);

            // Compute true 3D normals per vertex
            for (size_t i = 0; i < n; ++i) {
                const auto& curr = config.polygon[i];
                const auto& prev = config.polygon[(i + n - 1) % n];
                const auto& next = config.polygon[(i + 1) % n];

                Vec3 p_curr = {curr.x, curr.y, curr.z_front};
                Vec3 p_prev = {prev.x, prev.y, prev.z_front};
                Vec3 p_next = {next.x, next.y, next.z_front};

                Vec3 edge1 = p_next - p_curr;
                Vec3 edge2 = p_prev - p_curr;
                Vec3 norm = normalize3(cross3(edge1, edge2));
                if (norm.z < 0.0) norm = norm * -1.0; // Point toward camera

                front.vertices[i].x = (float)curr.x;
                front.vertices[i].y = (float)curr.y;
                front.vertices[i].z = (float)curr.z_front;
                front.vertices[i].nx = (float)norm.x;
                front.vertices[i].ny = (float)norm.y;
                front.vertices[i].nz = (float)norm.z;
                front.vertices[i].u = (float)(curr.x / config.texture_scale);
                front.vertices[i].v = (float)(curr.y / config.texture_scale);
            }
            out_mesh.submeshes.push_back(std::move(front));
        }
    }

    // ── Helper: Angle & slope detection ───────────────────────────────────────
    auto is_surface_edge = [&](size_t idx) -> bool {
        const auto& n0 = config.polygon[idx];
        const auto& n1 = config.polygon[(idx + 1) % n];
        if (n0.edge_type == Edge_Surface) return true;
        if (n0.edge_type == Edge_Wall || n0.edge_type == Edge_Overhang || n0.edge_type == Edge_Invisible) return false;
        double dx = n1.x - n0.x;
        double dy = n1.y - n0.y;
        double angle_deg = std::atan2(dy, dx) * (180.0 / kPi);
        if (angle_deg < 0.0) angle_deg += 360.0;
        double d = std::abs(angle_deg - 180.0);
        return std::min(d, 360.0 - d) < config.top_angle_deg;
    };

    // ── Submesh 2: Top Surface & Rounded Grass Crests ──────────────────────────
    if (config.generate_top) {
        ZenithSubmesh top;
        top.name = "TopSurface";
        top.texture_name = config.top_texture;

        double u_accum = 0.0;
        int bevel_segs = std::clamp(config.bevel_segments, 2, 16);

        for (size_t i = 0; i < n; ++i) {
            if (!is_surface_edge(i)) continue;

            size_t next_i = (i + 1) % n;
            const auto& n0 = config.polygon[i];
            const auto& n1 = config.polygon[next_i];

            double dx = n1.x - n0.x;
            double dy = n1.y - n0.y;
            double edge_len = std::sqrt(dx * dx + dy * dy);

            uint16_t base_idx = (uint16_t)top.vertices.size();
            double radius0 = n0.bevel_radius;
            double radius1 = n1.bevel_radius;

            // Generate parametric cylindrical arc bevel spanning from z_back to z_front
            for (int s = 0; s <= bevel_segs; ++s) {
                double t = (double)s / (double)bevel_segs;
                // Full wrap: 0 (back edge) -> pi/2 (top crest) -> pi (front roll)
                double angle = t * kPi;
                double sin_a = std::sin(angle);
                double cos_a = std::cos(angle);

                // Vertex at n0
                double z0 = n0.z_back + t * (n0.z_front - n0.z_back);
                double y_off0 = sin_a * radius0;
                Vertex3D v0;
                v0.x = (float)n0.x;
                v0.y = (float)(n0.y + y_off0);
                v0.z = (float)(z0 - cos_a * 5.0);
                v0.nx = 0.0f;
                v0.ny = (float)sin_a;
                v0.nz = (float)-cos_a;
                v0.u = (float)(u_accum / config.texture_scale);
                v0.v = (float)t;

                // Vertex at n1
                double z1 = n1.z_back + t * (n1.z_front - n1.z_back);
                double y_off1 = sin_a * radius1;
                Vertex3D v1;
                v1.x = (float)n1.x;
                v1.y = (float)(n1.y + y_off1);
                v1.z = (float)(z1 - cos_a * 5.0);
                v1.nx = 0.0f;
                v1.ny = (float)sin_a;
                v1.nz = (float)-cos_a;
                v1.u = (float)((u_accum + edge_len) / config.texture_scale);
                v1.v = (float)t;

                top.vertices.push_back(v0);
                top.vertices.push_back(v1);
            }

            // Quad strip triangle indices
            for (int s = 0; s < bevel_segs; ++s) {
                uint16_t i00 = base_idx + s * 2;
                uint16_t i01 = i00 + 1;
                uint16_t i10 = i00 + 2;
                uint16_t i11 = i00 + 3;

                top.indices.push_back(i00);
                top.indices.push_back(i10);
                top.indices.push_back(i01);

                top.indices.push_back(i01);
                top.indices.push_back(i10);
                top.indices.push_back(i11);
            }

            u_accum += edge_len;
        }

        if (!top.vertices.empty()) {
            out_mesh.submeshes.push_back(std::move(top));
        }
    }

    // ── Submesh 3: Side Skirts (Vertical Cliffs & Cave Underhangs) ─────────────
    {
        ZenithSubmesh side;
        side.name = "SideSkirts";
        side.texture_name = config.front_texture;

        double accum_dist = 0.0;
        for (size_t i = 0; i < n; ++i) {
            size_t next_i = (i + 1) % n;
            // Skip surface edges if top is generated
            if (config.generate_top && is_surface_edge(i)) {
                double dx = config.polygon[next_i].x - config.polygon[i].x;
                double dy = config.polygon[next_i].y - config.polygon[i].y;
                accum_dist += std::sqrt(dx * dx + dy * dy);
                continue;
            }

            const auto& n0 = config.polygon[i];
            const auto& n1 = config.polygon[next_i];

            double dx = n1.x - n0.x;
            double dy = n1.y - n0.y;
            double len = std::sqrt(dx * dx + dy * dy);
            Vec2 edge_norm = normalize2({dy, -dx});

            uint16_t base = (uint16_t)side.vertices.size();

            // 4 vertices per wall segment: quad connecting (n0_back, n1_back, n1_front, n0_front)
            Vertex3D v0_back, v1_back, v1_front, v0_front;
            v0_back.x = (float)n0.x; v0_back.y = (float)n0.y; v0_back.z = (float)n0.z_back;
            v0_back.nx = (float)edge_norm.x; v0_back.ny = (float)edge_norm.y; v0_back.nz = 0.0f;
            v0_back.u = (float)(accum_dist / config.texture_scale); v0_back.v = 0.0f;

            v1_back.x = (float)n1.x; v1_back.y = (float)n1.y; v1_back.z = (float)n1.z_back;
            v1_back.nx = (float)edge_norm.x; v1_back.ny = (float)edge_norm.y; v1_back.nz = 0.0f;
            v1_back.u = (float)((accum_dist + len) / config.texture_scale); v1_back.v = 0.0f;

            v1_front.x = (float)n1.x; v1_front.y = (float)n1.y; v1_front.z = (float)n1.z_front;
            v1_front.nx = (float)edge_norm.x; v1_front.ny = (float)edge_norm.y; v1_front.nz = 0.0f;
            v1_front.u = (float)((accum_dist + len) / config.texture_scale); v1_front.v = 1.0f;

            v0_front.x = (float)n0.x; v0_front.y = (float)n0.y; v0_front.z = (float)n0.z_front;
            v0_front.nx = (float)edge_norm.x; v0_front.ny = (float)edge_norm.y; v0_front.nz = 0.0f;
            v0_front.u = (float)(accum_dist / config.texture_scale); v0_front.v = 1.0f;

            side.vertices.push_back(v0_back);
            side.vertices.push_back(v1_back);
            side.vertices.push_back(v1_front);
            side.vertices.push_back(v0_front);

            side.indices.push_back(base + 0);
            side.indices.push_back(base + 1);
            side.indices.push_back(base + 2);

            side.indices.push_back(base + 0);
            side.indices.push_back(base + 2);
            side.indices.push_back(base + 3);

            accum_dist += len;
        }

        if (!side.vertices.empty()) {
            out_mesh.submeshes.push_back(std::move(side));
        }
    }

    // ── Submesh 4: Round Hat Domes (Caver Dome Math with Variable Z) ───────────
    for (size_t h_idx = 0; h_idx < config.hats.size(); ++h_idx) {
        const auto& hat = config.hats[h_idx];
        ZenithSubmesh hat_mesh;
        hat_mesh.name = "RoundHat_" + std::to_string(h_idx);
        hat_mesh.texture_name = config.top_texture;

        const int N = 18; // segments
        double base_y = config.polygon.empty() ? 0.0 : config.polygon[0].y;
        for (const auto& p : config.polygon) base_y = std::max(base_y, p.y);
        base_y += 0.05;

        const double r = std::max(1.0, hat.radius);
        const double h = std::max(1.0, hat.height);
        const double cx = hat.x;

        // Find closest polygon node to determine Z depth
        double z_front = 45.0, z_back = -45.0;
        double min_dist_sq = 1e18;
        for (const auto& node : config.polygon) {
            double d2 = (node.x - cx) * (node.x - cx) + (node.y - base_y) * (node.y - base_y);
            if (d2 < min_dist_sq) {
                min_dist_sq = d2;
                z_front = node.z_front;
                z_back = node.z_back;
            }
        }
        if (z_back > z_front) std::swap(z_back, z_front);

        std::vector<uint16_t> ring0(N + 1), ring1(N + 1);
        for (int t = 0; t <= N; ++t) {
            double phi = kPi * (double)t / (double)N;
            double px = cx + r * std::cos(phi);
            double py = base_y + h * std::sin(phi);
            Vec3 norm = normalize3({std::cos(phi), std::sin(phi), 0.0});

            ring0[t] = (uint16_t)hat_mesh.vertices.size();
            Vertex3D v0;
            v0.x = (float)px; v0.y = (float)py; v0.z = (float)z_back;
            v0.nx = (float)norm.x; v0.ny = (float)norm.y; v0.nz = (float)norm.z;
            v0.u = (float)(px / config.texture_scale); v0.v = (float)(py / config.texture_scale);
            hat_mesh.vertices.push_back(v0);

            ring1[t] = (uint16_t)hat_mesh.vertices.size();
            Vertex3D v1;
            v1.x = (float)px; v1.y = (float)py; v1.z = (float)z_front;
            v1.nx = (float)norm.x; v1.ny = (float)norm.y; v1.nz = (float)norm.z;
            v1.u = (float)(px / config.texture_scale); v1.v = (float)(py / config.texture_scale);
            hat_mesh.vertices.push_back(v1);
        }

        // Ridge sheet
        for (int t = 0; t < N; ++t) {
            uint16_t a = ring0[t], b = ring0[t + 1];
            uint16_t c = ring1[t + 1], d = ring1[t];
            hat_mesh.indices.push_back(a); hat_mesh.indices.push_back(b); hat_mesh.indices.push_back(c);
            hat_mesh.indices.push_back(a); hat_mesh.indices.push_back(c); hat_mesh.indices.push_back(d);
        }

        // End caps
        uint16_t cb = (uint16_t)hat_mesh.vertices.size();
        Vertex3D vb; vb.x = (float)cx; vb.y = (float)base_y; vb.z = (float)z_back;
        vb.nx = 0.0f; vb.ny = 0.0f; vb.nz = -1.0f;
        vb.u = (float)(cx / config.texture_scale); vb.v = (float)(base_y / config.texture_scale);
        hat_mesh.vertices.push_back(vb);
        for (int t = 0; t < N; ++t) {
            hat_mesh.indices.push_back(cb);
            hat_mesh.indices.push_back(ring0[t + 1]);
            hat_mesh.indices.push_back(ring0[t]);
        }

        uint16_t cf = (uint16_t)hat_mesh.vertices.size();
        Vertex3D vf; vf.x = (float)cx; vf.y = (float)base_y; vf.z = (float)z_front;
        vf.nx = 0.0f; vf.ny = 0.0f; vf.nz = 1.0f;
        vf.u = (float)(cx / config.texture_scale); vf.v = (float)(base_y / config.texture_scale);
        hat_mesh.vertices.push_back(vf);
        for (int t = 0; t < N; ++t) {
            hat_mesh.indices.push_back(cf);
            hat_mesh.indices.push_back(ring1[t]);
            hat_mesh.indices.push_back(ring1[t + 1]);
        }

        if (!hat_mesh.vertices.empty()) {
            out_mesh.submeshes.push_back(std::move(hat_mesh));
        }
    }

    return !out_mesh.submeshes.empty();
}

// ─── Protobuf Serialization ────────────────────────────────────────────────

std::vector<uint8_t> serialize_to_caver_protobuf(const ZenithGeneratedMesh& mesh) {
    std::vector<uint8_t> root;

    for (const auto& sm : mesh.submeshes) {
        std::vector<uint8_t> mesh_data;

        // 1. Packed Vertices (interleaved 32-byte struct: pos, norm, uv)
        std::vector<uint8_t> v_bytes;
        v_bytes.reserve(sm.vertices.size() * 32);
        for (const auto& v : sm.vertices) {
            uint8_t raw[32];
            std::memcpy(raw + 0,  &v.x, 4);
            std::memcpy(raw + 4,  &v.y, 4);
            std::memcpy(raw + 8,  &v.z, 4);
            std::memcpy(raw + 12, &v.nx, 4);
            std::memcpy(raw + 16, &v.ny, 4);
            std::memcpy(raw + 20, &v.nz, 4);
            std::memcpy(raw + 24, &v.u, 4);
            std::memcpy(raw + 28, &v.v, 4);
            v_bytes.insert(v_bytes.end(), raw, raw + 32);
        }
        encode_bytes(mesh_data, 1, v_bytes.data(), v_bytes.size()); // field 1 = vertices

        // 2. Packed Indices (16-bit unsigned short)
        std::vector<uint8_t> i_bytes;
        i_bytes.reserve(sm.indices.size() * 2);
        for (uint16_t idx : sm.indices) {
            uint8_t raw[2] = {(uint8_t)(idx & 0xFF), (uint8_t)(idx >> 8)};
            i_bytes.insert(i_bytes.end(), raw, raw + 2);
        }
        encode_bytes(mesh_data, 2, i_bytes.data(), i_bytes.size()); // field 2 = indices

        // 3. Material texture stem
        encode_string(mesh_data, 3, sm.texture_name);

        // Pack into repeated MeshData field (field 1 of Caver::Proto::Mesh)
        encode_bytes(root, 1, mesh_data.data(), mesh_data.size());
    }

    return root;
}

// ─── MeshPen & .rbc Container Implementation ───────────────────────────────

static const char kRbcMagic[16] = "RUBY-CANVAS-01\0";

bool rbc_encode_png(const std::vector<uint8_t>& png_in,
                    const RbcCanvasData& data,
                    std::vector<uint8_t>& out_rbc) {
    out_rbc = png_in; // Start with the valid PNG image

    // Serialize metadata payload
    std::vector<uint8_t> meta;
    meta.insert(meta.end(), kRbcMagic, kRbcMagic + 16);

    // Header strings & floats
    auto append_str = [&](const std::string& s) {
        uint16_t len = (uint16_t)s.size();
        meta.push_back((uint8_t)(len & 0xFF));
        meta.push_back((uint8_t)(len >> 8));
        meta.insert(meta.end(), s.begin(), s.end());
    };
    append_str(data.project_name);
    append_str(data.top_texture);
    append_str(data.front_texture);

    auto append_f32 = [&](float v) {
        uint8_t raw[4];
        std::memcpy(raw, &v, 4);
        meta.insert(meta.end(), raw, raw + 4);
    };
    append_f32(data.world_z);
    append_f32(data.base_depth_min);
    append_f32(data.base_depth_max);

    // Stroke points with pen width
    uint32_t num_strokes = (uint32_t)data.strokes.size();
    uint8_t ns_raw[4];
    std::memcpy(ns_raw, &num_strokes, 4);
    meta.insert(meta.end(), ns_raw, ns_raw + 4);

    for (const auto& stroke : data.strokes) {
        uint32_t count = (uint32_t)stroke.size();
        uint8_t c_raw[4];
        std::memcpy(c_raw, &count, 4);
        meta.insert(meta.end(), c_raw, c_raw + 4);

        for (const auto& pt : stroke) {
            append_f32(pt.x);
            append_f32(pt.y);
            append_f32(pt.pen_width);
            append_f32(pt.pressure);
        }
    }

    // Append metadata + 4-byte payload size trailer + 4-byte magic trailer
    uint32_t payload_size = (uint32_t)meta.size();
    uint8_t sz_raw[4];
    std::memcpy(sz_raw, &payload_size, 4);

    out_rbc.insert(out_rbc.end(), meta.begin(), meta.end());
    out_rbc.insert(out_rbc.end(), sz_raw, sz_raw + 4);
    out_rbc.push_back('R'); out_rbc.push_back('B'); out_rbc.push_back('C'); out_rbc.push_back('!');

    return true;
}

bool rbc_decode_png(const uint8_t* rbc_data, size_t size, RbcCanvasData& out_data) {
    if (!rbc_data || size < 24) return false;

    // Check trailer: "RBC!" at end
    if (rbc_data[size - 4] != 'R' || rbc_data[size - 3] != 'B' ||
        rbc_data[size - 2] != 'C' || rbc_data[size - 1] != '!') {
        return false;
    }

    uint32_t payload_size = 0;
    std::memcpy(&payload_size, rbc_data + size - 8, 4);
    if (payload_size + 8 > size) return false;

    const uint8_t* p = rbc_data + size - 8 - payload_size;
    if (std::memcmp(p, kRbcMagic, 14) != 0) return false;
    p += 16;

    auto read_str = [&]() -> std::string {
        uint16_t len = p[0] | (p[1] << 8);
        p += 2;
        std::string s((const char*)p, len);
        p += len;
        return s;
    };
    out_data.project_name = read_str();
    out_data.top_texture = read_str();
    out_data.front_texture = read_str();

    auto read_f32 = [&]() -> float {
        float v;
        std::memcpy(&v, p, 4);
        p += 4;
        return v;
    };
    out_data.world_z = read_f32();
    out_data.base_depth_min = read_f32();
    out_data.base_depth_max = read_f32();

    uint32_t num_strokes = 0;
    std::memcpy(&num_strokes, p, 4);
    p += 4;
    out_data.strokes.resize(num_strokes);

    for (uint32_t s = 0; s < num_strokes; ++s) {
        uint32_t count = 0;
        std::memcpy(&count, p, 4);
        p += 4;
        out_data.strokes[s].resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            out_data.strokes[s][i].x = read_f32();
            out_data.strokes[s][i].y = read_f32();
            out_data.strokes[s][i].pen_width = read_f32();
            out_data.strokes[s][i].pressure = read_f32();
        }
    }

    return true;
}

bool rbc_strokes_to_zenith_config(const RbcCanvasData& rbc,
                                  float simplification_tolerance,
                                  ZenithMeshConfig& out_config) {
    out_config.polygon.clear();
    out_config.top_texture = rbc.top_texture;
    out_config.front_texture = rbc.front_texture;
    out_config.world_z = rbc.world_z;

    if (rbc.strokes.empty()) return false;

    // Convert largest closed stroke to polygon
    const auto& main_stroke = rbc.strokes[0];
    if (main_stroke.size() < 3) return false;

    out_config.polygon.reserve(main_stroke.size());
    for (const auto& pt : main_stroke) {
        ZenithNode node;
        node.x = pt.x;
        node.y = pt.y;
        node.pen_thickness = pt.pen_width;

        // Map pen thickness to Z front/back depth & bevel radius!
        // Fat ink = thick 3D rock, thin ink = sharp edge
        double depth_scalar = std::clamp(pt.pen_width / 20.0, 0.5, 4.0);
        node.z_front = rbc.base_depth_max * depth_scalar;
        node.z_back = rbc.base_depth_min * depth_scalar;
        node.bevel_radius = std::clamp(pt.pen_width * 0.25, 2.0, 15.0);

        out_config.polygon.push_back(node);
    }

    return true;
}

// ─── FileRift Markup & Protobuf Scene Object Builders ──────────────────────

static std::string zenith_quote_bytes(const std::vector<uint8_t>& s) {
    const char* hexChars = "0123456789abcdef";
    std::string out;
    for (uint8_t uc : s) {
        if (uc == '"') out += "\\\"";
        else if (uc == '\\') out += "\\\\";
        else if (uc == '\t') out += "\\t";
        else if (uc == '\n') out += "\\n";
        else if (uc == '\r') out += "\\r";
        else if (uc >= 0x20 && uc < 0x7f) out += uc;
        else {
            out += "\\x";
            out += hexChars[uc >> 4];
            out += hexChars[uc & 0xf];
        }
    }
    return out;
}

static void pack_submesh_bytes(const ZenithSubmesh& sm,
                               std::vector<uint8_t>& out_v,
                               std::vector<uint8_t>& out_i) {
    out_v.clear();
    out_i.clear();
    out_v.reserve(sm.vertices.size() * 32);
    for (const auto& v : sm.vertices) {
        uint8_t raw[32];
        std::memcpy(raw + 0,  &v.x, 4);
        std::memcpy(raw + 4,  &v.y, 4);
        std::memcpy(raw + 8,  &v.z, 4);
        std::memcpy(raw + 12, &v.nx, 4);
        std::memcpy(raw + 16, &v.ny, 4);
        std::memcpy(raw + 20, &v.nz, 4);
        std::memcpy(raw + 24, &v.u, 4);
        std::memcpy(raw + 28, &v.v, 4);
        out_v.insert(out_v.end(), raw, raw + 32);
    }
    out_i.reserve(sm.indices.size() * 2);
    for (uint16_t idx : sm.indices) {
        uint8_t raw[2] = {(uint8_t)(idx & 0xFF), (uint8_t)(idx >> 8)};
        out_i.insert(out_i.end(), raw, raw + 2);
    }
}

std::string generate_ground_mesh_3d(const ZenithMeshConfig& config) {
    ZenithGeneratedMesh gm;
    if (!generate_zenith_mesh(config, gm) || config.polygon.size() < 3) return "";

    double min_x = 1e9, max_x = -1e9, min_y = 1e9, max_y = -1e9;
    double min_z = 1e9, max_z = -1e9;
    std::stringstream poly_stream;
    for (const auto& v : config.polygon) {
        poly_stream << "                    Vertex{ X : " << v.x << " Y : " << v.y << " }\n";
        min_x = std::min(min_x, v.x); max_x = std::max(max_x, v.x);
        min_y = std::min(min_y, v.y); max_y = std::max(max_y, v.y);
        min_z = std::min(min_z, v.z_back); max_z = std::max(max_z, v.z_front);
    }

    char aabb_str[256];
    snprintf(aabb_str, sizeof(aabb_str),
             "X : %f Y : %f Z : %f Width : %f Height : %f Depth : %f",
             min_x, min_y, min_z, max_x - min_x, max_y - min_y, max_z - min_z);
    char square_str[256];
    snprintf(square_str, sizeof(square_str),
             "X : %f Y : %f Width : %f Height : %f",
             min_x, min_y, max_x - min_x, max_y - min_y);

    std::stringstream ss;
    ss << "        Component{\n"
       << "            ClassName : 'GroundPolygon'\n"
       << "            Identifier : 980\n"
       << "            GroundPolygonComponent{\n"
       << "                Polygon{\n"
       << poly_stream.str()
       << "                    Convex : 0\n"
       << "                    Closed : 1\n"
       << "                }\n"
       << "                Collides : 1\n"
       << "                MinDepth : " << min_z << "\n"
       << "                MaxDepth : " << max_z << "\n"
       << "            }\n"
       << "        }\n"
       << "        Component{\n"
       << "            ClassName : 'GroundMesh'\n"
       << "            Identifier : 981\n"
       << "            GroundMeshComponent{\n"
       << "                LocalAabb{ " << aabb_str << " }\n";

    for (const auto& sm : gm.submeshes) {
        std::vector<uint8_t> v_bytes, i_bytes;
        pack_submesh_bytes(sm, v_bytes, i_bytes);

        if (sm.name == "FrontFace") {
            ss << "                FrontMesh{\n"
               << "                    NumVertices : " << sm.vertices.size() << "\n"
               << "                    NumFaces : " << (sm.indices.size() / 3) << "\n"
               << "                    Indices{ ValueType : 4 ValuesPerVertex : 1 Stride : 2 DataOffset : 0 }\n"
               << "                    Vertices{ ValueType : 7 ValuesPerVertex : 3 Stride : 32 DataOffset : 0 }\n"
               << "                    Normals{ ValueType : 7 ValuesPerVertex : 3 Stride : 32 DataOffset : 12 }\n"
               << "                    TexCoordSet{ ValueType : 7 ValuesPerVertex : 2 Stride : 32 DataOffset : 24 }\n"
               << "                    Material{\n"
               << "                        AmbientColor{ R : 1.0 G : 1.0 B : 1.0 A : 1.0 }\n"
               << "                        DiffuseColor{ R : 1.0 G : 1.0 B : 1.0 A : 1.0 }\n"
               << "                        SpecularColor{ R : 1.0 G : 1.0 B : 1.0 A : 1.0 }\n"
               << "                        Shininess : 0.0\n"
               << "                        Texture{ Name : '" << sm.texture_name << "' PixelFormat : 1 ImageType : 2 }\n"
               << "                    }\n"
               << "                    BoundingBox{ " << aabb_str << " }\n"
               << "                    VertexData : '" << zenith_quote_bytes(v_bytes) << "'\n"
               << "                    IndexData : '" << zenith_quote_bytes(i_bytes) << "'\n"
               << "                }\n";
        } else {
            ss << "                SurfaceMesh{\n"
               << "                    NumVertices : " << sm.vertices.size() << "\n"
               << "                    NumFaces : " << (sm.indices.size() / 3) << "\n"
               << "                    Indices{ ValueType : 4 ValuesPerVertex : 1 Stride : 2 DataOffset : 0 }\n"
               << "                    Vertices{ ValueType : 7 ValuesPerVertex : 3 Stride : 32 DataOffset : 0 }\n"
               << "                    Normals{ ValueType : 7 ValuesPerVertex : 3 Stride : 32 DataOffset : 12 }\n"
               << "                    TexCoordSet{ ValueType : 7 ValuesPerVertex : 2 Stride : 32 DataOffset : 24 }\n"
               << "                    Material{\n"
               << "                        AmbientColor{ R : 1.0 G : 1.0 B : 1.0 A : 1.0 }\n"
               << "                        DiffuseColor{ R : 1.0 G : 1.0 B : 1.0 A : 1.0 }\n"
               << "                        SpecularColor{ R : 1.0 G : 1.0 B : 1.0 A : 1.0 }\n"
               << "                        Shininess : 0.0\n"
               << "                        Texture{ Name : '" << sm.texture_name << "' PixelFormat : 1 ImageType : 2 }\n"
               << "                    }\n"
               << "                    BoundingBox{ " << aabb_str << " }\n"
               << "                    VertexData : '" << zenith_quote_bytes(v_bytes) << "'\n"
               << "                    IndexData : '" << zenith_quote_bytes(i_bytes) << "'\n"
               << "                }\n";
        }
    }

    ss << "                Color{ R : 1.0 G : 1.0 B : 1.0 A : 1.0 }\n"
       << "            }\n"
       << "        }\n"
       << "        Component{\n"
       << "            ClassName : 'GroundMeshGenerator'\n"
       << "            Identifier : 982\n"
       << "            GroundMeshGeneratorComponent{\n"
       << "                GroundPolygonId : 980\n"
       << "                TargetMeshId : 981\n"
       << "                FrontTextureMappingId : 985\n"
       << "                SurfaceTextureMappingId : 984\n"
       << "                RandomSeed : " << config.random_seed << "\n"
       << "                HorizNoise : 0.0\n"
       << "                MeshType : 1\n"
       << "                SurfaceWidth : 80.0\n"
       << "                HatHeight : 25.0\n"
       << "                HatWidthOffset1 : 5.0\n"
       << "                HatWidthOffset2 : 5.0\n"
       << "            }\n"
       << "        }\n"
       << "        Component{\n"
       << "            ClassName : 'CollisionShape'\n"
       << "            Identifier : 983\n"
       << "            ParentComponentIdentifier : 980\n"
       << "            ShapeComponent{\n"
       << "                Polygon{\n"
       << poly_stream.str()
       << "                    Convex : 0\n"
       << "                    Closed : 1\n"
       << "                }\n"
       << "            }\n"
       << "            CollisionShapeComponent{\n"
       << "                IsGround : 1\n"
       << "                MinDepth : " << min_z << "\n"
       << "                MaxDepth : " << max_z << "\n"
       << "                Enabled : 1\n"
       << "            }\n"
       << "        }\n"
       << "        Component{\n"
       << "            ClassName : 'TextureMapping'\n"
       << "            Identifier : 984\n"
       << "            TextureMappingComponent{\n"
       << "                TextureName : '" << config.top_texture << "'\n"
       << "                Scale : " << config.texture_scale << "\n"
       << "                MappingType : 1\n"
       << "                Square{ " << square_str << " }\n"
       << "            }\n"
       << "        }\n"
       << "        Component{\n"
       << "            ClassName : 'TextureMapping'\n"
       << "            Identifier : 985\n"
       << "            TextureMappingComponent{\n"
       << "                TextureName : '" << config.front_texture << "'\n"
       << "                Scale : " << config.texture_scale << "\n"
       << "                MappingType : 0\n"
       << "                Square{ " << square_str << " }\n"
       << "            }\n"
       << "        }\n";

    return ss.str();
}

std::string generate_ground_mesh_object_3d(const ZenithMeshConfig& config,
                                           const std::string& identifier,
                                           double depth,
                                           const ZenithComponentIds* ids) {
    ZenithGeneratedMesh gm;
    if (!generate_zenith_mesh(config, gm) || config.polygon.size() < 3) return "";

    ZenithComponentIds default_ids;
    const ZenithComponentIds& cid = ids ? *ids : default_ids;

    double min_x = 1e9, max_x = -1e9, min_y = 1e9, max_y = -1e9;
    double min_z = 1e9, max_z = -1e9;
    for (const auto& v : config.polygon) {
        min_x = std::min(min_x, v.x); max_x = std::max(max_x, v.x);
        min_y = std::min(min_y, v.y); max_y = std::max(max_y, v.y);
        min_z = std::min(min_z, v.z_back); max_z = std::max(max_z, v.z_front);
    }

    // Protobuf writers
    auto make_v2 = [](double x, double y) {
        proto::Writer w;
        w.write_float_field(1, (float)x);
        w.write_float_field(2, (float)y);
        return w;
    };

    auto make_rect = [](double x, double y, double w, double h) {
        proto::Writer wr;
        wr.write_float_field(1, (float)x);
        wr.write_float_field(2, (float)y);
        wr.write_float_field(3, (float)w);
        wr.write_float_field(4, (float)h);
        return wr;
    };

    auto make_color = [](float r, float g, float b, float a) {
        proto::Writer w;
        w.write_float_field(1, r); w.write_float_field(2, g);
        w.write_float_field(3, b); w.write_float_field(4, a);
        return w;
    };

    auto make_box_pb = [](float x, float y, float z, float w, float h, float d) {
        proto::Writer bx;
        bx.write_float_field(1, x); bx.write_float_field(2, y); bx.write_float_field(3, z);
        bx.write_float_field(4, w); bx.write_float_field(5, h); bx.write_float_field(6, d);
        return bx;
    };

    auto make_mesh_data_pb = [](int val_type, int vals_per_v, int stride, int offset) {
        proto::Writer md;
        md.write_varint_field(1, (uint64_t)val_type);
        md.write_varint_field(2, (uint64_t)vals_per_v);
        md.write_varint_field(3, (uint64_t)stride);
        md.write_varint_field(4, (uint64_t)offset);
        return md;
    };

    auto make_submesh_pb = [&](const ZenithSubmesh& sm) {
        proto::Writer w;
        std::vector<uint8_t> v_bytes, i_bytes;
        pack_submesh_bytes(sm, v_bytes, i_bytes);

        const int num_v = (int)sm.vertices.size();
        const int num_f = (int)(sm.indices.size() / 3);

        w.write_varint_field(1, (uint64_t)num_v);
        w.write_varint_field(2, (uint64_t)num_f);
        if (!i_bytes.empty())
            w.write_nested_field(3, make_mesh_data_pb(4, 1, 2, 0)); // Indices
        w.write_nested_field(4, make_mesh_data_pb(7, 3, 32, 0));  // Pos
        w.write_nested_field(5, make_mesh_data_pb(7, 3, 32, 12)); // Norm
        w.write_nested_field(6, make_mesh_data_pb(7, 2, 32, 24)); // UV

        // Material
        proto::Writer mat;
        mat.write_nested_field(1, make_color(1, 1, 1, 1));
        mat.write_nested_field(2, make_color(1, 1, 1, 1));
        mat.write_nested_field(3, make_color(1, 1, 1, 1));
        mat.write_float_field(4, 0.0f);
        proto::Writer tex_desc;
        tex_desc.write_string_field(1, sm.texture_name);
        tex_desc.write_varint_field(2, 1); // RGBA8888
        tex_desc.write_varint_field(4, 1); // PNG
        mat.write_nested_field(5, tex_desc);
        w.write_nested_field(10, mat);

        w.write_nested_field(11, make_box_pb((float)min_x, (float)min_y, (float)min_z,
                                            (float)(max_x - min_x), (float)(max_y - min_y), (float)(max_z - min_z)));
        w.write_bytes_field(50, std::string(v_bytes.begin(), v_bytes.end()));
        if (!i_bytes.empty())
            w.write_bytes_field(51, std::string(i_bytes.begin(), i_bytes.end()));
        return w;
    };

    // 1. Polygon
    proto::Writer poly;
    for (const auto& v : config.polygon) {
        poly.write_nested_field(1, make_v2(v.x, v.y));
    }
    poly.write_varint_field(2, 0); // Non-convex
    poly.write_varint_field(3, 1); // Closed

    // 2. GroundPolygonComponent
    proto::Writer gpc;
    gpc.write_nested_field(2, poly);
    gpc.write_varint_field(3, 1);
    gpc.write_float_field(4, (float)min_z);
    gpc.write_float_field(5, (float)max_z);
    proto::Writer comp_gpc;
    comp_gpc.write_string_field(1, "GroundPolygon");
    comp_gpc.write_varint_field(2, (uint64_t)cid.polygon_id);
    comp_gpc.write_nested_field(110, gpc);

    // 3. GroundMeshComponent
    proto::Writer gmc;
    gmc.write_nested_field(7, make_rect(min_x, min_y, max_x - min_x, max_y - min_y));
    for (const auto& sm : gm.submeshes) {
        if (sm.name == "FrontFace") {
            gmc.write_nested_field(9, make_submesh_pb(sm)); // FrontMesh
        } else {
            gmc.write_nested_field(8, make_submesh_pb(sm)); // SurfaceMesh
        }
    }
    gmc.write_nested_field(10, make_color(1, 1, 1, 1));
    proto::Writer comp_gmc;
    comp_gmc.write_string_field(1, "GroundMesh");
    comp_gmc.write_varint_field(2, (uint64_t)cid.mesh_id);
    comp_gmc.write_nested_field(111, gmc);

    // 4. GroundMeshGeneratorComponent
    proto::Writer ggc;
    ggc.write_varint_field(1, (uint64_t)cid.polygon_id);
    ggc.write_varint_field(2, (uint64_t)cid.mesh_id);
    ggc.write_varint_field(3, (uint64_t)cid.tm_front_id);
    ggc.write_varint_field(4, (uint64_t)cid.tm_surface_id);
    ggc.write_varint_field(5, (uint64_t)config.random_seed);
    ggc.write_float_field(6, 0.0f);
    ggc.write_varint_field(7, 1);
    ggc.write_float_field(8, 80.0f);
    ggc.write_float_field(9, 25.0f);
    ggc.write_float_field(10, 5.0f);
    ggc.write_float_field(11, 5.0f);
    proto::Writer comp_ggc;
    comp_ggc.write_string_field(1, "GroundMeshGenerator");
    comp_ggc.write_varint_field(2, (uint64_t)cid.generator_id);
    comp_ggc.write_nested_field(112, ggc);

    // 5. CollisionShape
    proto::Writer shape;
    shape.write_nested_field(3, poly);
    proto::Writer csc;
    csc.write_varint_field(2, 1);
    csc.write_float_field(6, (float)min_z);
    csc.write_float_field(7, (float)max_z);
    csc.write_varint_field(11, 1);
    proto::Writer comp_col;
    comp_col.write_string_field(1, "CollisionShape");
    comp_col.write_varint_field(2, (uint64_t)cid.collision_id);
    comp_col.write_varint_field(4, (uint64_t)cid.polygon_id);
    comp_col.write_nested_field(120, shape);
    comp_col.write_nested_field(121, csc);

    // 6. TextureMapping surface & front
    proto::Writer tm_surf;
    tm_surf.write_string_field(1, config.top_texture);
    tm_surf.write_float_field(2, (float)config.texture_scale);
    tm_surf.write_varint_field(3, 1);
    tm_surf.write_nested_field(4, make_rect(min_x, min_y, max_x - min_x, max_y - min_y));
    proto::Writer comp_tm_surf;
    comp_tm_surf.write_string_field(1, "TextureMapping");
    comp_tm_surf.write_varint_field(2, (uint64_t)cid.tm_surface_id);
    comp_tm_surf.write_nested_field(113, tm_surf);

    proto::Writer tm_front;
    tm_front.write_string_field(1, config.front_texture);
    tm_front.write_float_field(2, (float)config.texture_scale);
    tm_front.write_varint_field(3, 0);
    tm_front.write_nested_field(4, make_rect(min_x, min_y, max_x - min_x, max_y - min_y));
    proto::Writer comp_tm_front;
    comp_tm_front.write_string_field(1, "TextureMapping");
    comp_tm_front.write_varint_field(2, (uint64_t)cid.tm_front_id);
    comp_tm_front.write_nested_field(113, tm_front);

    // Assembly into single Object
    proto::Writer obj;
    obj.write_string_field(1, identifier);
    obj.write_float_field(6, (float)depth);
    obj.write_nested_field(7, comp_gpc);
    obj.write_nested_field(7, comp_gmc);
    obj.write_nested_field(7, comp_ggc);
    obj.write_nested_field(7, comp_col);
    obj.write_nested_field(7, comp_tm_surf);
    obj.write_nested_field(7, comp_tm_front);

    return obj.to_string();
}

} // namespace zenith
