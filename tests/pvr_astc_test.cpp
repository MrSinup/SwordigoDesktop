// pvr_astc_test.cpp — Verify ASTC PVR decompression in Ruby texture pipeline
#include "platform/pvr_loader.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <string>

// Strong definition for the weak default in src/platform/data_path.cpp
std::string g_instance_assets_dir = "assets";

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    std::cout << "[pvr_astc_test] Testing ASTC .pvr decompression...\n";

    // Test 1: BalloonTex_astc.pvr from Native_SDK-master (if path exists on device)
    const char* sample_path = "/run/media/quantumcreeper/TVPG/Prenxy Packages/SwordigoTools/Native_SDK-master/examples/assets/Balloons/BalloonTex_astc.pvr";
    auto data = read_file(sample_path);
    if (data.empty()) {
        std::cout << "  SKIP  Sample ASTC file not found at " << sample_path << " (environment without mount)\n";
        return EXIT_SUCCESS;
    }

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    bool ok = pvr_decode_to_rgba(data.data(), data.size(), rgba, w, h);
    if (!ok) {
        std::cerr << "  FAIL  pvr_decode_to_rgba failed for BalloonTex_astc.pvr\n";
        return EXIT_FAILURE;
    }
    if (w != 1024 || h != 1024 || rgba.size() != 1024 * 1024 * 4) {
        std::cerr << "  FAIL  unexpected dimensions " << w << "x" << h << ", buffer size: " << rgba.size() << "\n";
        return EXIT_FAILURE;
    }

    // Verify image data contains non-zero pixels
    bool non_zero = false;
    for (size_t i = 0; i < rgba.size(); i += 4) {
        if (rgba[i] != 0 || rgba[i+1] != 0 || rgba[i+2] != 0) {
            non_zero = true;
            break;
        }
    }
    if (!non_zero) {
        std::cerr << "  FAIL  decoded image is entirely blank\n";
        return EXIT_FAILURE;
    }

    std::cout << "  PASS  BalloonTex_astc.pvr decoded to 1024x1024 RGBA with valid content\n";
    std::cout << "[pvr_astc_test] ALL PASS\n";
    return EXIT_SUCCESS;
}
