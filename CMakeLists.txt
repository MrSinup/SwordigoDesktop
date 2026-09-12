cmake_minimum_required(VERSION 3.16)
project(SwordigoDesktop VERSION 4.5.0 LANGUAGES C CXX ASM)

# Build directly into the source tree's distribution folder — no separate
# `cmake --install` step needed. Executables land in <dist>/, shared libs in
# <dist>/libs/.
#
# The dist folder is `bin` for native (Linux) builds and `binw` for Windows
# builds (incl. MinGW cross-compiles) so Windows .exe/.dll artifacts never land
# in — or clobber — the Linux `bin/` tree. Override with -DSWORDIGO_DIST_DIR=...
if (NOT DEFINED SWORDIGO_DIST_DIR)
    if (WIN32)
        set(SWORDIGO_DIST_DIR "binw")
    else()
        set(SWORDIGO_DIST_DIR "bin")
    endif()
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/${SWORDIGO_DIST_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/${SWORDIGO_DIST_DIR}/libs")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/${SWORDIGO_DIST_DIR}/libs")

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Modular build. Each component lives in cmake/components/*.cmake and defines
# its own targets; shared platform/option/helper logic is in cmake/*.cmake.
# ---------------------------------------------------------------------------

include(cmake/SwordigoOptions.cmake)     # options + cache paths
include(cmake/SwordigoPlatform.cmake)    # includes, defines, platform flags
include(cmake/SwordigoDeps.cmake)        # external dependencies
include(cmake/SwordigoHelpers.cmake)     # swordigo_target()/swordigo_library()

# Component order matters: libraries before the executables that consume them.
include(cmake/components/swcore.cmake)
include(cmake/components/swgui.cmake)
include(cmake/components/swfmt_swpod.cmake)
include(cmake/components/filerift.cmake)
include(cmake/components/caver.cmake)      # Ruby's own recovered Caver runtime
include(cmake/components/swgfx.cmake)
include(cmake/components/swemu.cmake)
include(cmake/components/swordfare.cmake)
include(cmake/components/swordfare_boot.cmake)
include(cmake/components/ruby.cmake)
include(cmake/components/ruby_cli.cmake)
include(cmake/components/ruby_gg.cmake)

# Guest + third-party build steps and install rules.
include(cmake/components/sre.cmake)
include(cmake/components/sre13.cmake)
include(cmake/components/misc.cmake)

if (BUILD_TESTING)
    add_executable(trampoline_mgr_test
        tests/trampoline_mgr_test.cpp
        ${SRC_DIR}/platform/arm64_reloc.cpp)
    target_include_directories(trampoline_mgr_test PRIVATE ${SRC_DIR})
    add_test(NAME trampoline_mgr_test COMMAND trampoline_mgr_test)

    add_executable(sre_hud_coin_regression_test
        tests/sre_hud_coin_regression_test.cpp)
    target_compile_definitions(sre_hud_coin_regression_test PRIVATE
        SRE_SOURCE_DIR="${SRC_DIR}/sre/sre12")
    add_test(NAME sre_hud_coin_regression_test COMMAND sre_hud_coin_regression_test)

    add_executable(filerift_smoke tests/filerift_smoke.cpp)
    target_include_directories(filerift_smoke PRIVATE ${SRC_DIR} ${SRC_DIR}/sre/base/lua/src)
    target_link_libraries(filerift_smoke PRIVATE filerift swpod OpenGL::GL)
    add_test(NAME filerift_smoke COMMAND filerift_smoke)

    # Smart-save / corruption regression: scene round trip must be structure-
    # identical (in-place apply, no reload) and encode errors must throw before
    # the target file is touched. Data file is optional (SKIP when absent).
    add_executable(scene_smart_save_test tests/scene_smart_save_test.cpp)
    target_include_directories(scene_smart_save_test PRIVATE ${SRC_DIR})
    target_compile_definitions(scene_smart_save_test PRIVATE
        SWORDIGO_SRC_DIR="${SRC_DIR}")
    target_link_libraries(scene_smart_save_test PRIVATE filerift swpod OpenGL::GL)
    add_test(NAME scene_smart_save_test COMMAND scene_smart_save_test)

    # TEMPORARY perf probe (timing on real scenes — remove after diagnosis).
    # TEMPORARY perf driver: real RubyMainWindow + the user's real scene, offscreen.
    set(SCENE_PERF_DRIVER_SOURCES ${RUBY_GG_SOURCES})
    list(REMOVE_ITEM SCENE_PERF_DRIVER_SOURCES ${SRC_DIR}/ruby/main.cpp)
    add_executable(scene_perf_driver
        tests/scene_perf_driver.cpp
        ${SCENE_PERF_DRIVER_SOURCES})
    set_target_properties(scene_perf_driver PROPERTIES
        AUTOMOC ON AUTORCC ON AUTOUIC ON)
    target_compile_definitions(scene_perf_driver PRIVATE
        SWORDIGO_NO_IMGUI
        BATCH_CONVERTER_NO_UI
        SWORDIGO_SRC_DIR="${SRC_DIR}")
    target_include_directories(scene_perf_driver PRIVATE
        ${SRC_DIR}
        ${SRC_DIR}/ruby
        ${SRC_DIR}/ruby/git/include
        ${SRC_DIR}/tools
        ${SRC_DIR}/tools/ufbx
        ${SRC_DIR}/stb
        ${SRC_DIR}/platform
        ${SRC_DIR}/sre/base/lua/src
        ${MPG123_INCLUDE_DIRS}
        ${VORBISFILE_INCLUDE_DIRS})
    target_link_libraries(scene_perf_driver PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network
        OpenGL::GL ZLIB::ZLIB swcore swfmt swpod filerift git2_static
        SDL3::SDL3 ${SDL3_IMAGE_LINK}
        ${MPG123_LIBRARIES} ${VORBISFILE_LIBRARIES}
        ${SWORDIGO_LIBM} ${SWORDIGO_SOCKLIB})
    if (NOT WIN32)
        target_link_libraries(scene_perf_driver PRIVATE dl pthread util)
    endif()
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(scene_perf_driver PRIVATE rt)
    endif()

    add_executable(scene_perf_probe tests/scene_perf_probe.cpp
        ${SRC_DIR}/ruby/viewport/ruby_picking.cpp)
    target_include_directories(scene_perf_probe PRIVATE ${SRC_DIR})
    target_compile_definitions(scene_perf_probe PRIVATE
        SWORDIGO_NO_IMGUI
        BATCH_CONVERTER_NO_UI
        SWORDIGO_SRC_DIR="${SRC_DIR}")
    target_link_libraries(scene_perf_probe PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network
        OpenGL::GL ZLIB::ZLIB swcore swfmt swpod filerift git2_static
        SDL3::SDL3 ${SDL3_IMAGE_LINK}
        ${MPG123_LIBRARIES} ${VORBISFILE_LIBRARIES}
        ${SWORDIGO_LIBM} ${SWORDIGO_SOCKLIB})
    if (NOT WIN32)
        target_link_libraries(scene_perf_probe PRIVATE dl pthread util)
    endif()
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(scene_perf_probe PRIVATE rt)
    endif()
    add_test(NAME scene_perf_probe COMMAND scene_perf_probe)

    # Scene-text corruption regression: a .scene that became FileRift text
    # (single or double banner) must re-encode to a binary that loads back to
    # the same structure via av::scene_load_bytes — the viewport's in-memory
    # recovery path. Data file is optional (SKIP when absent).
    add_executable(scene_text_recovery_test tests/scene_text_recovery_test.cpp)
    target_include_directories(scene_text_recovery_test PRIVATE ${SRC_DIR})
    target_compile_definitions(scene_text_recovery_test PRIVATE
        SWORDIGO_SRC_DIR="${SRC_DIR}")
    target_link_libraries(scene_text_recovery_test PRIVATE filerift swpod OpenGL::GL)
    add_test(NAME scene_text_recovery_test COMMAND scene_text_recovery_test)

    # Scale-gizmo payload propagation (web editor `scaleObjectData` parity):
    # scaling an object must also scale LocalAABB + ShapeComponent geometry
    # (rect/circle/polygon) + CollisionShapeComponent depth extents, with the
    # model-object dominant-axis-uniform rule. Pure RAM unit test, no GL/Qt.
    add_executable(scene_scale_payload_test tests/scene_scale_payload_test.cpp)
    target_include_directories(scene_scale_payload_test PRIVATE ${SRC_DIR})
    target_link_libraries(scene_scale_payload_test PRIVATE swpod swcore)
    add_test(NAME scene_scale_payload_test COMMAND scene_scale_payload_test)

    # Schema-driven component field editors (master TODO 2.1): the inspector's
    # field-editor path (scene_component_fields + scene_set_component_field)
    # must round-trip every wire type — VARINT, I32 float, printable LEN,
    # nested LEN — plus unknown binary fields from newer game versions, and
    # the Program (Lua) helpers must both store the source AND regenerate the
    # compiled bytecode the engine actually runs. RAM unit test, no Qt; links
    # filerift because the Lua-aware Program writer lives with the host Lua.
    add_executable(component_field_editors_test tests/component_field_editors_test.cpp)
    target_include_directories(component_field_editors_test PRIVATE ${SRC_DIR})
    target_link_libraries(component_field_editors_test PRIVATE filerift swpod swcore)
    add_test(NAME component_field_editors_test COMMAND component_field_editors_test)

    # Caver Core C1 — the ObjectLibrary registry (library_manager.h): load by
    # name through the search path, identity from ObjectLibrary.Name, template
    # lookup with owner reporting, the content-hash fast path, on-disk hot swap
    # (added/removed templates), in-memory swap, imports and missing diagnostics.
    # Pure RAM/FS unit test, no Qt/GL; the corpus check skips when assets are absent.
    add_executable(library_manager_test tests/library_manager_test.cpp)
    target_include_directories(library_manager_test PRIVATE ${SRC_DIR})
    target_link_libraries(library_manager_test PRIVATE caver swpod swcore Threads::Threads)
    add_test(NAME library_manager_test COMMAND library_manager_test)

    # Template Palette + template hierarchy (master TODO 2.3/2.4): the .scl/
    # .pod scan, the link → override → materialize → reset lifecycle and the
    # Model-component / LocalAABB wire builders. Pure RAM unit test.
    add_executable(template_sources_test tests/template_sources_test.cpp)
    target_include_directories(template_sources_test PRIVATE ${SRC_DIR})
    target_link_libraries(template_sources_test PRIVATE swpod swcore)
    add_test(NAME template_sources_test COMMAND template_sources_test)

    # SCL studio mutations (master TODO 2.4a): add/rename/remove a template in
    # an ObjectLibrary while preserving every other byte, plus a byte-exact
    # save/reload round trip. Pure RAM unit test.
    add_executable(scl_studio_test tests/scl_studio_test.cpp)
    target_include_directories(scl_studio_test PRIVATE ${SRC_DIR})
    target_link_libraries(scl_studio_test PRIVATE swpod swcore)
    add_test(NAME scl_studio_test COMMAND scl_studio_test)

    # APK session import + registry (master TODO 4.1a): builds a tiny fake
    # APK with the shared zip writer, imports it, asserts byte-identical
    # extraction + session.json registry round trip + validation failures.
    # $RUBY_SESSIONS_DIR keeps the test hermetic (no ~/.ruby writes).
    add_executable(apk_session_test
        tests/apk_session_test.cpp
        ${SRC_DIR}/platform/zip_archive.cpp
        ${SRC_DIR}/tools/apk_session.cpp)
    target_include_directories(apk_session_test PRIVATE ${SRC_DIR})
    target_link_libraries(apk_session_test PRIVATE ZLIB::ZLIB)
    add_test(NAME apk_session_test COMMAND apk_session_test)

    # Ground Mesh Studio: simple-polygon enforcement (repaint-lag fix) + the
    # "Add to Scene" object round trip. Runs headless (offscreen platform).
    add_executable(ground_mesh_studio_test
        tests/ground_mesh_studio_test.cpp
        ${SRC_DIR}/ruby/tools/ground_mesh_studio.cpp)
    set_target_properties(ground_mesh_studio_test PROPERTIES AUTOMOC ON)
    target_include_directories(ground_mesh_studio_test PRIVATE ${SRC_DIR})
    target_link_libraries(ground_mesh_studio_test PRIVATE
        filerift swpod Qt6::Widgets Qt6::Gui Qt6::Core OpenGL::GL)
    add_test(NAME ground_mesh_studio_test COMMAND ground_mesh_studio_test)
    set_tests_properties(ground_mesh_studio_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Title-bar window controls must render visibly (painted icons). This
    # guards the frameless window's Close/Min/Max buttons against font-glyph
    # fallbacks that made them invisible on several Linux font stacks (KDE
    # Plasma Wayland in particular — no WM decorations there either).
    add_executable(titlebar_render_test
        tests/titlebar_render_test.cpp
        ${SRC_DIR}/ruby/editor/ruby_title_bar.cpp)
    set_target_properties(titlebar_render_test PROPERTIES AUTOMOC ON)
    target_include_directories(titlebar_render_test PRIVATE ${SRC_DIR})
    target_link_libraries(titlebar_render_test PRIVATE
        Qt6::Widgets Qt6::Gui Qt6::Core)
    add_test(NAME titlebar_render_test COMMAND titlebar_render_test)
    set_tests_properties(titlebar_render_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Asset browser quick sync: files created by an external process (and a
    # root directory that appears after set_root_path) must show up in the
    # panel's tree without any manual refresh.
    add_executable(asset_sync_test
        tests/asset_sync_test.cpp
        ${SRC_DIR}/ruby/panels/asset_browser_panel.cpp)
    set_target_properties(asset_sync_test PROPERTIES AUTOMOC ON)
    target_include_directories(asset_sync_test PRIVATE ${SRC_DIR})
    target_link_libraries(asset_sync_test PRIVATE
        Qt6::Widgets Qt6::Gui Qt6::Core)
    add_test(NAME asset_sync_test COMMAND asset_sync_test)
    set_tests_properties(asset_sync_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Audio viewer (WAV/MP3/OGG): decodes a real RIFF WAV, asserts metadata +
    # waveform, exercises the async load path and corrupt-input handling. The
    # SDL3 / mpg123 / vorbisfile decoders are already REQUIRED project deps.
    add_executable(audio_viewer_test
        tests/audio_viewer_test.cpp
        ${SRC_DIR}/ruby/panels/audio_viewer_panel.cpp)
    set_target_properties(audio_viewer_test PROPERTIES AUTOMOC ON)
    target_include_directories(audio_viewer_test PRIVATE
        ${SRC_DIR} ${MPG123_INCLUDE_DIRS} ${VORBISFILE_INCLUDE_DIRS})
    target_link_libraries(audio_viewer_test PRIVATE
        Qt6::Widgets Qt6::Gui Qt6::Core
        SDL3::SDL3 ${SDL3_IMAGE_LINK}
        ${MPG123_LIBRARIES} ${VORBISFILE_LIBRARIES})
    add_test(NAME audio_viewer_test COMMAND audio_viewer_test)
    set_tests_properties(audio_viewer_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(pvr_astc_test tests/pvr_astc_test.cpp)
    target_include_directories(pvr_astc_test PRIVATE ${SRC_DIR})
    target_link_libraries(pvr_astc_test PRIVATE swfmt swcore OpenGL::GL)
    add_test(NAME pvr_astc_test COMMAND pvr_astc_test)

    # The real Viewport3DWidget + its backends, shared by the scene-editing
    # acceptance tests below (offscreen GL, Mesa swrast; data file optional).
    set(RUBY_VIEWPORT_TEST_SOURCES
        ${SRC_DIR}/ruby/viewport/viewport_3d_widget.cpp
        ${SRC_DIR}/ruby/viewport/ruby_gizmo.cpp
        ${SRC_DIR}/ruby/viewport/ruby_picking.cpp
        ${SRC_DIR}/ruby/viewport/scene_loading_overlay.cpp
        # The forward render pipeline the viewport widget owns (task 3.1 added
        # water/portal/particle + the FBO chain): viewport_3d_widget.cpp
        # instantiates these directly, so their vtables must link here too.
        ${SRC_DIR}/ruby/render/viewport_shader.cpp
        ${SRC_DIR}/ruby/render/light_rig.cpp
        ${SRC_DIR}/ruby/render/fbo_chain.cpp
        ${SRC_DIR}/ruby/render/ssao_pass.cpp
        ${SRC_DIR}/ruby/render/post_pass.cpp
        ${SRC_DIR}/ruby/render/water_renderer.cpp
        ${SRC_DIR}/ruby/render/portal_renderer.cpp
        ${SRC_DIR}/ruby/render/particle_engine.cpp
        ${SRC_DIR}/ruby/core/project_context.cpp
        ${SRC_DIR}/tools/pod_loader.cpp
        ${SRC_DIR}/tools/pod_writer.cpp
        ${SRC_DIR}/tools/pod_convert.cpp
        ${SRC_DIR}/tools/scene_loader.cpp
        ${SRC_DIR}/tools/filerift.cpp
        ${SRC_DIR}/tools/scene_asset_resolver.cpp
        ${SRC_DIR}/tools/scene_workspace.cpp
        ${SRC_DIR}/tools/scene_entity.cpp
        ${SRC_DIR}/tools/scene_physics.cpp
        ${SRC_DIR}/tools/scene_collision.cpp
        ${SRC_DIR}/tools/scene_terrain.cpp
        ${SRC_DIR}/tools/scene_lua.cpp
        ${SRC_DIR}/tools/scene_game.cpp
        ${SRC_DIR}/tools/scene_creator.cpp
        ${SRC_DIR}/tools/scene_generator.cpp
        ${SRC_DIR}/tools/scene_generator_v2.cpp
        ${SRC_DIR}/tools/scene_generator_v2_3d.cpp
        ${SRC_DIR}/tools/scene_generator_v3.cpp
        ${SRC_DIR}/tools/scene_v3_db.cpp
        ${SRC_DIR}/tools/boulder.cpp
        ${SRC_DIR}/tools/rubymesh.cpp
        ${SRC_DIR}/tools/obj_loader.cpp
        ${SRC_DIR}/tools/ani_loader.cpp
        ${SRC_DIR}/tools/scn_loader.cpp
        ${SRC_DIR}/tools/map_loader.cpp
        ${SRC_DIR}/tools/gltf_export.cpp
        ${SRC_DIR}/tools/gltf_import.cpp
        ${SRC_DIR}/tools/gltf_bridge.cpp
        ${SRC_DIR}/tools/fbx_import.cpp
        ${SRC_DIR}/tools/intellij.cpp
        ${SRC_DIR}/tools/batch_converter.cpp
        ${SRC_DIR}/tools/ufbx/ufbx.c
        ${SRC_DIR}/tools/tiny_gltf_v3.c
        ${SRC_DIR}/platform/pvr_loader.cpp
        ${SRC_DIR}/platform/pvrtc_decoder.cpp
        ${SRC_DIR}/platform/astc_decoder.cpp
        ${SRC_DIR}/platform/win_dll_dir.cpp)
    function(ruby_viewport_test NAME SOURCE)
        add_executable(${NAME}
            ${SOURCE}
            ${RUBY_VIEWPORT_TEST_SOURCES})
        set_target_properties(${NAME} PROPERTIES AUTOMOC ON)
        target_compile_definitions(${NAME} PRIVATE
            SWORDIGO_NO_IMGUI
            BATCH_CONVERTER_NO_UI
            SWORDIGO_SRC_DIR="${SRC_DIR}")
        target_include_directories(${NAME} PRIVATE
            ${SRC_DIR}
            ${SRC_DIR}/ruby
            ${SRC_DIR}/tools
            ${SRC_DIR}/tools/ufbx
            ${SRC_DIR}/stb
            ${SRC_DIR}/platform
            ${SRC_DIR}/sre/base/lua/src)
        target_link_libraries(${NAME} PRIVATE
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets
            OpenGL::GL ZLIB::ZLIB swcore swfmt swpod filerift
            SDL3::SDL3 ${SDL3_IMAGE_LINK} ${SWORDIGO_LIBM} ${SWORDIGO_SOCKLIB})
        if (NOT WIN32)
            target_link_libraries(${NAME} PRIVATE dl pthread util)
        endif()
        if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(${NAME} PRIVATE rt)
        endif()
        add_test(NAME ${NAME} COMMAND ${NAME})
        set_tests_properties(${NAME} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endfunction()

    # Scene-object clipboard editing: copy/paste/duplicate/delete/move through
    # the real Viewport3DWidget public API against a real .scene file.
    ruby_viewport_test(scene_edit_acceptance_test tests/scene_edit_acceptance_test.cpp)

    # In-scene mesh editor (projection lock): camera locks to the ground
    # polygon's plane, synthesized mouse drags move a vertex, and committing
    # regenerates the object's ground geometry through boulder.
    ruby_viewport_test(scene_mesh_edit_test tests/scene_mesh_edit_test.cpp)

    if (SWORDIGO_BUILD_RUBY_GG)
        # Ctrl+S must preserve viewport edits. Regression: the FileRift text
        # sync refreshed the cached buffer but not the IDE's visible text, so
        # scene_has_user_text_edits() misread our own sync as user typing and
        # the save reverted the edit. Drives the real RubyMainWindow through
        # its private slots (offscreen Qt + Mesa swrast).
        set(SCENE_SAVE_TEST_SOURCES ${RUBY_GG_SOURCES})
        list(REMOVE_ITEM SCENE_SAVE_TEST_SOURCES ${SRC_DIR}/ruby/main.cpp)
        add_executable(scene_save_preserves_edit_test
            tests/scene_save_preserves_edit_test.cpp
            ${SCENE_SAVE_TEST_SOURCES})
        set_target_properties(scene_save_preserves_edit_test PROPERTIES
            AUTOMOC ON AUTORCC ON AUTOUIC ON)
        target_compile_definitions(scene_save_preserves_edit_test PRIVATE
            SWORDIGO_NO_IMGUI
            BATCH_CONVERTER_NO_UI
            SWORDIGO_SRC_DIR="${SRC_DIR}")
        target_include_directories(scene_save_preserves_edit_test PRIVATE
            ${SRC_DIR}
            ${SRC_DIR}/ruby
            ${SRC_DIR}/ruby/git/include
            ${SRC_DIR}/tools
            ${SRC_DIR}/tools/ufbx
            ${SRC_DIR}/stb
            ${SRC_DIR}/platform
            ${SRC_DIR}/sre/base/lua/src
            ${MPG123_INCLUDE_DIRS}
            ${VORBISFILE_INCLUDE_DIRS})
        target_link_libraries(scene_save_preserves_edit_test PRIVATE
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network
            OpenGL::GL ZLIB::ZLIB swcore swfmt swpod filerift git2_static
            SDL3::SDL3 ${SDL3_IMAGE_LINK}
            ${MPG123_LIBRARIES} ${VORBISFILE_LIBRARIES}
            ${SWORDIGO_LIBM} ${SWORDIGO_SOCKLIB})
        if (NOT WIN32)
            target_link_libraries(scene_save_preserves_edit_test PRIVATE dl pthread util)
        endif()
        if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(scene_save_preserves_edit_test PRIVATE rt)
        endif()
        add_test(NAME scene_save_preserves_edit_test COMMAND scene_save_preserves_edit_test)
        set_tests_properties(scene_save_preserves_edit_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        # Scene-wide snapshot undo (web-editor model): duplicate/delete/
        # visibility/component-paste edits land on the per-scene QUndoStack as
        # full scene-binary snapshots; undo/redo restores them (structural via
        # rebuild, in-place via the fast apply). Also drives the inspector's
        # componentPasted signal end-to-end (fresh type id + undoable).
        set(SCENE_UNDO_TEST_SOURCES ${RUBY_GG_SOURCES})
        list(REMOVE_ITEM SCENE_UNDO_TEST_SOURCES ${SRC_DIR}/ruby/main.cpp)
        add_executable(scene_undo_test
            tests/scene_undo_test.cpp
            ${SCENE_UNDO_TEST_SOURCES})
        set_target_properties(scene_undo_test PROPERTIES
            AUTOMOC ON AUTORCC ON AUTOUIC ON)
        target_compile_definitions(scene_undo_test PRIVATE
            SWORDIGO_NO_IMGUI
            BATCH_CONVERTER_NO_UI
            SWORDIGO_SRC_DIR="${SRC_DIR}")
        target_include_directories(scene_undo_test PRIVATE
            ${SRC_DIR}
            ${SRC_DIR}/ruby
            ${SRC_DIR}/ruby/git/include
            ${SRC_DIR}/tools
            ${SRC_DIR}/tools/ufbx
            ${SRC_DIR}/stb
            ${SRC_DIR}/platform
            ${SRC_DIR}/sre/base/lua/src
            ${MPG123_INCLUDE_DIRS}
            ${VORBISFILE_INCLUDE_DIRS})
        target_link_libraries(scene_undo_test PRIVATE
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network
            OpenGL::GL ZLIB::ZLIB swcore swfmt swpod filerift git2_static
            SDL3::SDL3 ${SDL3_IMAGE_LINK}
            ${MPG123_LIBRARIES} ${VORBISFILE_LIBRARIES}
            ${SWORDIGO_LIBM} ${SWORDIGO_SOCKLIB})
        if (NOT WIN32)
            target_link_libraries(scene_undo_test PRIVATE dl pthread util)
        endif()
        if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(scene_undo_test PRIVATE rt)
        endif()
        add_test(NAME scene_undo_test COMMAND scene_undo_test)
        set_tests_properties(scene_undo_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    if (SWORDIGO_BUILD_RUBY)
        add_executable(styx_smoke tests/styx_smoke.cpp ${SRC_DIR}/tools/intellij.cpp)
        swordigo_target(styx_smoke)
        target_link_libraries(styx_smoke PRIVATE swgui)
        add_test(NAME styx_smoke COMMAND styx_smoke)

        # RubyMesh .rbm data layer (pure std C++17, no external links).
        add_executable(rubymesh_roundtrip_test
            tests/rubymesh_roundtrip_test.cpp
            ${SRC_DIR}/tools/rubymesh.cpp)
        swordigo_target(rubymesh_roundtrip_test)
        add_test(NAME rubymesh_roundtrip_test COMMAND rubymesh_roundtrip_test)

        # Raylib ray picking & matrix calculation test
        add_executable(ruby_picking_test
            tests/ruby_picking_test.cpp
            ${SRC_DIR}/ruby/viewport/ruby_picking.cpp)
        swordigo_target(ruby_picking_test)
        target_link_libraries(ruby_picking_test PRIVATE Qt6::Core Qt6::Gui)
        add_test(NAME ruby_picking_test COMMAND ruby_picking_test)

        # Asset browser panel test
        add_executable(asset_browser_test
            tests/asset_browser_test.cpp
            ${SRC_DIR}/ruby/panels/asset_browser_panel.cpp)
        set_target_properties(asset_browser_test PROPERTIES AUTOMOC ON)
        target_include_directories(asset_browser_test PRIVATE ${SRC_DIR})
        target_link_libraries(asset_browser_test PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core)
        add_test(NAME asset_browser_test COMMAND asset_browser_test)
        set_tests_properties(asset_browser_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
