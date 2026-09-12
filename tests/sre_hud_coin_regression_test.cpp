#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

static std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    assert(file && "failed to open regression input");
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

int main() {
    const std::filesystem::path source_dir = SRE_SOURCE_DIR;
    const std::string scene = read_file(source_dir / "sre_scene_update.c");
    const std::string shifter = read_file(source_dir / "sre_scene_shifter.c");
    const std::string api = read_file(source_dir / "sre_mini_api.c");
    const std::string gui = read_file(source_dir / "sre_gui_native.c");

    assert(scene.find("coins == 0 && g_sre_player_coins > 0") == std::string::npos);
    assert(scene.find("g_orig_GameSceneView_Update_fn(self, deltaTime);") != std::string::npos);
    assert(scene.find("sre_GameSceneView_ExportState(self);") != std::string::npos);
    assert(scene.find("g_sre_player_mana_level = *(int*)(gamestate + 0xC4);") !=
           std::string::npos);
    assert(scene.find("#if 0  /* Disabled — see relay passthrough above */") !=
           std::string::npos);
    assert(scene.find("stale object quarantined") != std::string::npos);
    assert(scene.find("*(uint64_t*)(base + 0x20) = 0;") == std::string::npos);
    assert(api.find("val > 0 || g_sre_player_coins <= 0") == std::string::npos);
    assert(api.find("c==0) and _G.__last_coins>0") == std::string::npos);

    assert(gui.find("((pfn_DrawRect_N)g_orig_GUIButton_DrawRect)") != std::string::npos);
    assert(gui.find("((pfn_DrawRect_N)g_orig_GUILabel_DrawRect)") != std::string::npos);
    assert(gui.find("((pfn_DrawRect_N)g_orig_GUIFrameView_DrawRect)") != std::string::npos);

    // GameOverViewController respawn: must defer to host and pass valid view
    const std::string caver = read_file(source_dir / "sre_caver.h");
    assert(caver.find("typedef void (*pfn_GameOverVC_DidContinue)(void*, void*);") != std::string::npos);
    assert(caver.find("g_Camera_EvaluateViewMatrix") != std::string::npos);
    assert(gui.find("sre_gameover_respawn_tick") != std::string::npos);
    assert(gui.find("g_sre_gameover_respawn_controller") != std::string::npos);
    assert(gui.find("didContinue(self, 0)") == std::string::npos);

    const std::string caver_c = read_file(source_dir / "sre_caver.c");
    assert(caver_c.find("sre_float_is_bad") != std::string::npos);

    // ARM64 SceneLoadingView state starts near +0x180. The historical
    // +0x48/+0x50 guesses overwrite inherited GUIView members during portals.
    assert(shifter.find("SLVIEW_OFF_PROGRESS") == std::string::npos);
    assert(shifter.find("SLVIEW_OFF_LOAD_COMPLETE") == std::string::npos);
    assert(shifter.find("*(uint64_t*)gvc != g_swordigo_base + 0x6cb880ULL") !=
           std::string::npos);
    return 0;
}
