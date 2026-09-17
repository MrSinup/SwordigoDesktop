#pragma once
// ============================================================================
// boulderx.h — Zenith Mesh (BoulderX) Procedural Geometry Engine
//   Next-generation procedural terrain, 3D variable-Z extrusion, Ear-Clipping
//   polygon triangulation, parametric "Round Hat" crests, continuous UV
//   unwrapping, and MeshPen .rbc (Ruby Canvas) container.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace zenith {

// ─── Core Vertex & Profile Nodes ───────────────────────────────────────────

enum EdgeType : uint8_t {
    Edge_Auto = 0,       // Angle-based classification (< max_slope = surface, else wall)
    Edge_Surface = 1,    // Forced walking surface / grass roll
    Edge_Wall = 2,       // Forced vertical cliff / stone drop
    Edge_Overhang = 3,   // Underside cave ceiling
    Edge_Invisible = 4   // Transparent / pass-through boundary
};

struct ZenithNode {
    double x = 0.0;
    double y = 0.0;
    double z_front = 45.0;       // True 3D: variable front extrusion depth per node
    double z_back = -45.0;       // True 3D: variable back extrusion depth per node
    double bevel_radius = 5.0;   // Curve radius for top edge roll
    double pen_thickness = 20.0; // MeshPen ink thickness from drawing (maps to Z depth)
    EdgeType edge_type = Edge_Auto;
};

struct HatDome {
    double x = 0.0;
    double y = 0.0;
    double radius = 60.0;
    double height = 40.0;
};

struct ZenithMeshConfig {
    std::vector<ZenithNode> polygon;
    std::vector<HatDome> hats;
    double world_z = 40.0;               // Anchor scene depth layer
    double top_angle_deg = 35.0;         // Angle threshold for walking surface
    bool generate_top = true;
    bool generate_front = true;
    bool generate_back = true;
    int bevel_segments = 6;              // Smoothness of the grass round roll (2..16)
    std::string top_texture = "fire_grass";
    std::string front_texture = "graveyard_ground";
    double texture_scale = 250.0;
    uint32_t random_seed = 1291618994u;
};

// ─── 3D Vertex & Mesh Output ───────────────────────────────────────────────

struct Vertex3D {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

struct ZenithSubmesh {
    std::string name;
    std::string texture_name;
    std::vector<Vertex3D> vertices;
    std::vector<uint16_t> indices;
};

struct ZenithGeneratedMesh {
    std::vector<ZenithSubmesh> submeshes;
    size_t total_vertices() const;
    size_t total_triangles() const;
};

// ─── Ear-Clipping Polygon Triangulation ─────────────────────────────────────
// Robust non-convex polygon triangulation. Unlike boulder's triangle-fan, this
// cleanly decomposes arbitrary concave, winding, L-shaped, and cave polygons
// into valid triangles without self-intersection artifacts.
bool triangulate_polygon(const std::vector<std::pair<double, double>>& pts,
                         std::vector<uint16_t>& out_indices);

// ─── Procedural Synthesis & Export ─────────────────────────────────────────

// Builds a complete 3D Zenith mesh with front face, rounded walking crests,
// side skirts, and optional back face.
bool generate_zenith_mesh(const ZenithMeshConfig& config, ZenithGeneratedMesh& out_mesh);

// Serializes the generated mesh into native Caver Protobuf bytes suitable for
// direct insertion into a .scene or .swdmp file.
std::vector<uint8_t> serialize_to_caver_protobuf(const ZenithGeneratedMesh& mesh);

// Component ID mappings for FileRift scene serialization
struct ZenithComponentIds {
    int polygon_id = 980;
    int mesh_id = 981;
    int generator_id = 982;
    int collision_id = 983;
    int tm_surface_id = 984;
    int tm_front_id = 985;
};

// Generates human-readable FileRift .scene GroundMesh markup with 3D Zenith geometry
std::string generate_ground_mesh_3d(const ZenithMeshConfig& config);

// Builds a complete GroundMesh scene object directly as binary Protobuf bytes
// carrying GroundPolygon, GroundMesh, CollisionShape, and TextureMapping.
std::string generate_ground_mesh_object_3d(const ZenithMeshConfig& config,
                                           const std::string& identifier,
                                           double depth,
                                           const ZenithComponentIds* ids = nullptr);

// ─── MeshPen & .rbc (Ruby Canvas) Container ────────────────────────────────
// Dual-personality format: Standard PNG image + embedded metadata chunk
// ("RUBY-CANVAS-01") storing vector strokes, pen widths, and depth parameters.

struct RbcStrokePoint {
    float x, y;
    float pen_width;     // Ink thickness in canvas units
    float pressure;      // 0.0..1.0 (stylus) or 1.0 (mouse)
};

struct RbcCanvasData {
    std::string project_name = "CanvasMesh";
    std::string top_texture = "fire_grass";
    std::string front_texture = "graveyard_ground";
    float world_z = 40.0f;
    float base_depth_min = -45.0f;
    float base_depth_max = 45.0f;
    std::vector<std::vector<RbcStrokePoint>> strokes;
};

// Embeds RbcCanvasData into a PNG buffer.
bool rbc_encode_png(const std::vector<uint8_t>& png_in,
                    const RbcCanvasData& data,
                    std::vector<uint8_t>& out_rbc);

// Extracts RbcCanvasData from an .rbc PNG file.
bool rbc_decode_png(const uint8_t* rbc_data, size_t size, RbcCanvasData& out_data);

// Converts an RbcCanvasData stroke set into a polygonal ZenithMeshConfig.
// Ink thickness automatically scales Z-depth and bevel radius!
bool rbc_strokes_to_zenith_config(const RbcCanvasData& rbc,
                                  float simplification_tolerance,
                                  ZenithMeshConfig& out_config);

} // namespace zenith

namespace boulderx = zenith;
