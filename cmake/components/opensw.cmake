# ============================================================================
# Component: opensw — boot the recovered Swordigo runtime into a real level.
#
# src/ruby/caver/game/ is the *game* layer on top of the recovered engine in
# src/ruby/caver/:
#
#   game_data.cpp            — GameData (gamedata.gdata) + Map (test.scmap)
#   game_state.cpp           — GameState / CharacterState / LevelState (a save)
#   player_profile.cpp       — PlayerProfile: CreateProfile, LoadGameStateFromProtobuf
#   game_control.cpp         — GameControlButton -> player input
#   game_scene_controller.cpp— InitWithScene / SpawnHeroAt / CreateHeroObjectAt /
#                              AddHeroObjectToScene / GameControlButton* / Update,
#                              plus the engine's own camera (fov 0.34907 rad,
#                              near 50, far 20000, focus offset (0,187,1190)).
#   game_renderer.cpp        — the draw path: av::scene_load's level geometry,
#                              swk:: transforms, av::render_* passes, PostFX blit.
#   opensw_main.cpp          — the executable: profile -> level -> hero -> window.
#
# Rendering reuses the studio's stack rather than a second renderer:
#   av::      scene_loader, pod_loader, av_renderer, scene_asset_resolver
#   swk::     object_world_matrix / object_render_matrix (scene_workspace)
# which live in swpod / swfmt / swcore. SDL3 owns the window and GL context.
#
# There is no menu scene and no save selection: the boot starts at the level the
# save names, exactly as the engine does after a load.
#
# The binary is written to bin/ like every other tool, and a root-level `opensw`
# symlink is created so it can be run as ./opensw from the project root.
# ============================================================================

add_executable(opensw
    # av:: asset discovery (resolve_pod / texture_candidates). It is compiled
    # into ruby_gg and the viewport tests the same way: no library owns it yet.
    ${SRC_DIR}/tools/scene_asset_resolver.cpp
    ${SRC_DIR}/ruby/caver/game/game_data.cpp
    ${SRC_DIR}/ruby/caver/game/game_state.cpp
    ${SRC_DIR}/ruby/caver/game/player_profile.cpp
    ${SRC_DIR}/ruby/caver/game/game_control.cpp
    ${SRC_DIR}/ruby/caver/game/game_scene_controller.cpp
    ${SRC_DIR}/ruby/caver/game/game_renderer.cpp
    ${SRC_DIR}/ruby/caver/render/gl_pipeline.cpp
    ${SRC_DIR}/ruby/caver/game/opensw_main.cpp)

swordigo_target(opensw)
target_include_directories(opensw PRIVATE
    ${SRC_DIR}
    ${SRC_DIR}/ruby
    ${SRC_DIR}/tools
    ${SRC_DIR}/tools/ufbx
    ${SRC_DIR}/stb
    ${SRC_DIR}/platform
    ${SRC_DIR}/sre/base/lua/src)

# The scene workspace helpers (swk::) hide their ImVec2 shim behind this define;
# the renderer wants the plain one, without pulling ImGui into the shell.
target_compile_definitions(opensw PRIVATE
    SWORDIGO_NO_IMGUI
    BATCH_CONVERTER_NO_UI)

# caver  = the recovered runtime (components, behaviours, Lua host, SCL registry)
# swpod  = av:: scene loader + av_renderer + swk:: transforms + pod loader
# swfmt  = PVR/ETC1 texture decode (pvr_load_texture)
# swcore = platform paths / IO
target_link_libraries(opensw PRIVATE
    caver swpod swfmt swcore filerift
    SDL3::SDL3 ${SDL3_IMAGE_LINK} OpenGL::GL
    Threads::Threads ZLIB::ZLIB ${SWORDIGO_LIBM} ${SWORDIGO_SOCKLIB})

if (NOT WIN32)
    target_link_libraries(opensw PRIVATE dl)
endif()

# swpod/caver build as shared libraries; make sure the binary finds them without
# needing LD_LIBRARY_PATH.
if (UNIX AND NOT APPLE)
    set_target_properties(opensw PROPERTIES
        BUILD_RPATH "$<TARGET_FILE_DIR:swpod>;$<TARGET_FILE_DIR:caver>")
endif()

# Root-level launcher: `./opensw` from the project root after a build.
add_custom_command(TARGET opensw POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "bin/opensw" "${CMAKE_SOURCE_DIR}/opensw"
    COMMENT "opensw: linking ${CMAKE_SOURCE_DIR}/opensw -> bin/opensw")
