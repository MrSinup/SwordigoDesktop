// ============================================================================
// boulderx_test.cpp — Test suite for Zenith Mesh (BoulderX) Engine
// ============================================================================

#include "tools/boulderx.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <vector>

void test_concave_triangulation() {
    std::cout << "[Test] Running Concave Ear-Clipping Triangulation..." << std::endl;

    // 1. "L"-shaped concave polygon (6 vertices)
    // (0,0) -> (4,0) -> (4,1) -> (1,1) -> (1,4) -> (0,4)
    std::vector<std::pair<double, double>> l_shape = {
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 1.0},
        {1.0, 1.0}, {1.0, 4.0}, {0.0, 4.0}
    };

    std::vector<uint16_t> indices;
    bool ok = zenith::triangulate_polygon(l_shape, indices);
    assert(ok && "Triangulation of L-shape should succeed");
    assert(indices.size() == 12 && "6-vertex polygon must triangulate to 4 triangles (12 indices)");

    // 2. Cave / U-trough (8 vertices)
    std::vector<std::pair<double, double>> cave_trough = {
        {0.0, 0.0}, {6.0, 0.0}, {6.0, 5.0}, {5.0, 5.0},
        {5.0, 1.0}, {1.0, 1.0}, {1.0, 5.0}, {0.0, 5.0}
    };
    indices.clear();
    ok = zenith::triangulate_polygon(cave_trough, indices);
    assert(ok && "Triangulation of Cave Trough should succeed");
    assert(indices.size() == 18 && "8-vertex polygon must triangulate to 6 triangles (18 indices)");

    std::cout << "  ✓ Concave Ear-Clipping passed cleanly!" << std::endl;
}

void test_3d_zenith_mesh_synthesis() {
    std::cout << "[Test] Running 3D Zenith Mesh Synthesis with Variable Z..." << std::endl;

    zenith::ZenithMeshConfig cfg;
    cfg.world_z = 40.0;
    cfg.top_texture = "fire_grass";
    cfg.front_texture = "graveyard_ground";
    cfg.bevel_segments = 4;

    // A rocky hill with variable Z front/back depth per vertex!
    // Notice node 1 (apex) bulges out toward camera (z_front = 70.0)
    cfg.polygon = {
        {0.0,   0.0,  40.0, -40.0, 5.0, 20.0, zenith::Edge_Wall},
        {100.0, 60.0, 75.0, -50.0, 8.0, 35.0, zenith::Edge_Surface}, // apex with fat bevel & deep Z
        {200.0, 0.0,  40.0, -40.0, 5.0, 20.0, zenith::Edge_Wall},
        {100.0, -30.0, 30.0, -30.0, 3.0, 15.0, zenith::Edge_Overhang}
    };

    zenith::ZenithGeneratedMesh mesh;
    bool ok = zenith::generate_zenith_mesh(cfg, mesh);
    assert(ok && "Zenith mesh synthesis should succeed");
    assert(mesh.submeshes.size() >= 2 && "Should generate FrontFace and TopSurface");

    std::cout << "  Generated submeshes: " << mesh.submeshes.size() << std::endl;
    std::cout << "  Total vertices: " << mesh.total_vertices() << std::endl;
    std::cout << "  Total triangles: " << mesh.total_triangles() << std::endl;

    // Verify Protobuf byte stream serialization
    auto pb_bytes = zenith::serialize_to_caver_protobuf(mesh);
    assert(!pb_bytes.empty() && "Protobuf serialization must not be empty");
    std::cout << "  Caver Protobuf serialized size: " << pb_bytes.size() << " bytes" << std::endl;

    // Verify FileRift .scene GroundMesh markup generation
    std::string filerift_markup = zenith::generate_ground_mesh_3d(cfg);
    assert(!filerift_markup.empty() && "FileRift markup must not be empty");
    assert(filerift_markup.find("GroundPolygon") != std::string::npos && "Markup must contain GroundPolygon");
    assert(filerift_markup.find("GroundMesh") != std::string::npos && "Markup must contain GroundMesh");
    assert(filerift_markup.find("FrontMesh") != std::string::npos && "Markup must contain FrontMesh");
    assert(filerift_markup.find("CollisionShape") != std::string::npos && "Markup must contain CollisionShape");
    std::cout << "  FileRift markup size: " << filerift_markup.size() << " bytes" << std::endl;

    // Verify direct binary Protobuf Scene Object generation
    std::string scene_obj_bytes = zenith::generate_ground_mesh_object_3d(cfg, "ZenithRock3D", 40.0);
    assert(!scene_obj_bytes.empty() && "Protobuf scene object binary must not be empty");
    std::cout << "  Scene Object Protobuf binary size: " << scene_obj_bytes.size() << " bytes" << std::endl;

    std::cout << "  ✓ 3D Mesh, FileRift markup & Protobuf Scene Object generation passed!" << std::endl;
}

void test_rbc_container() {
    std::cout << "[Test] Running .rbc (Ruby Canvas) MeshPen container test..." << std::endl;

    // Dummy PNG byte array
    std::vector<uint8_t> dummy_png = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // PNG magic
        0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R',
        0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x73, 0x7A, 0x7A, 0xF4,
        0x00, 0x00, 0x00, 0x00, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82
    };

    zenith::RbcCanvasData canvas;
    canvas.project_name = "FloatingFortress";
    canvas.top_texture = "castle_grass";
    canvas.front_texture = "stone_bricks";
    canvas.world_z = 50.0f;

    // One stroke of 4 points with variable ink width
    canvas.strokes.push_back({
        {0.0f, 0.0f, 15.0f, 1.0f},
        {50.0f, 80.0f, 45.0f, 1.0f}, // Thick ink stroke
        {120.0f, 80.0f, 40.0f, 1.0f},
        {160.0f, 0.0f, 10.0f, 1.0f}
    });

    std::vector<uint8_t> rbc_file;
    bool enc_ok = zenith::rbc_encode_png(dummy_png, canvas, rbc_file);
    assert(enc_ok && "RBC encoding should succeed");
    assert(rbc_file.size() > dummy_png.size() && "RBC file must contain payload");

    zenith::RbcCanvasData restored;
    bool dec_ok = zenith::rbc_decode_png(rbc_file.data(), rbc_file.size(), restored);
    assert(dec_ok && "RBC decoding should succeed");
    assert(restored.project_name == "FloatingFortress");
    assert(restored.strokes.size() == 1);
    assert(restored.strokes[0].size() == 4);
    assert(restored.strokes[0][1].pen_width == 45.0f && "Pen width must match");

    // Convert strokes to ZenithMeshConfig
    zenith::ZenithMeshConfig zenith_cfg;
    bool conv_ok = zenith::rbc_strokes_to_zenith_config(restored, 1.0f, zenith_cfg);
    assert(conv_ok && "Converting RBC to Zenith configuration should succeed");
    assert(zenith_cfg.polygon.size() == 4);
    // Point 1 had 45.0 pen width -> should have deep extrusion
    assert(zenith_cfg.polygon[1].z_front > 45.0 && "Thick pen stroke should enlarge 3D Z depth!");

    // Generate output .rbc file for user: a rectangle mesh with varied pen width / varied Z
    zenith::RbcCanvasData rect_canvas;
    rect_canvas.project_name = "VariedZRectangle";
    rect_canvas.top_texture = "fire_grass";
    rect_canvas.front_texture = "graveyard_ground";
    rect_canvas.world_z = 40.0f;
    rect_canvas.base_depth_min = -45.0f;
    rect_canvas.base_depth_max = 45.0f;

    // Rectangle: 4 corners with varied pen width (15, 30, 60, 20) -> varied 3D Z front and back!
    rect_canvas.strokes.push_back({
        {0.0f,   0.0f,   15.0f, 1.0f}, // bottom-left (thin ink -> shallow Z)
        {200.0f, 0.0f,   30.0f, 1.0f}, // bottom-right (medium ink)
        {200.0f, 100.0f, 60.0f, 1.0f}, // top-right (very thick ink -> deep Z front bulge)
        {0.0f,   100.0f, 20.0f, 1.0f}  // top-left (regular ink)
    });

    std::vector<uint8_t> rect_rbc;
    bool rect_enc = zenith::rbc_encode_png(dummy_png, rect_canvas, rect_rbc);
    assert(rect_enc && "Rectangle RBC encoding should succeed");

    std::ofstream out_file("sample_rectangle_varied_z.rbc", std::ios::binary);
    if (out_file.is_open()) {
        out_file.write(reinterpret_cast<const char*>(rect_rbc.data()), rect_rbc.size());
        out_file.close();
        std::cout << "  ✓ Exported sample_rectangle_varied_z.rbc (" << rect_rbc.size() << " bytes)" << std::endl;
    }

    std::cout << "  ✓ .rbc MeshPen encoding, decoding, and auto-3D depth mapping passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Zenith Mesh (BoulderX) Engine Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    test_concave_triangulation();
    test_3d_zenith_mesh_synthesis();
    test_rbc_container();

    std::cout << "========================================" << std::endl;
    std::cout << "  ALL ZENITH MESH TESTS PASSED! 🎉" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
