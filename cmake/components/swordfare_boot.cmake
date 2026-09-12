# ============================================================================
# Component: swordfare_boot — the main launcher executable (bin/swordfare).
# Static FFmpeg + all component libraries get linked into this one binary.
# ============================================================================

add_executable(swordfare_boot
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/platform/arm64_reloc.cpp
    ${SRC_DIR}/platform/display.cpp
    ${SRC_DIR}/platform/pod_ipc.cpp
    ${SRC_DIR}/platform/loading_screen.cpp
    ${SRC_DIR}/platform/crash_dialog.cpp
    ${SRC_DIR}/platform/openswordigo_host.cpp
    ${SRC_DIR}/platform/gui.cpp
    ${SRC_DIR}/platform/input_config.cpp
    ${SRC_DIR}/platform/binary_selector.cpp
    ${SRC_DIR}/platform/ffmpeg_dyn.cpp)
set_target_properties(swordfare_boot PROPERTIES OUTPUT_NAME swordfare AUTOMOC ON)
swordigo_target(swordfare_boot)

target_link_libraries(swordfare_boot PRIVATE
    swcore swgui swfmt swpod filerift swgfx swemu swordfare
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
    SDL3::SDL3 ${SDL3_IMAGE_LINK} ${VORBISFILE_LIBRARIES} ${MPG123_LIBRARIES}
    ZLIB::ZLIB OpenAL::OpenAL OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS} ${SWORDIGO_LIBM} ${SWORDIGO_SOCKLIB})

# shm_open / mmap for the Ruby GG engine-pod frame ring (pod_ipc.cpp).
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(swordfare_boot PRIVATE rt)
endif()


if (SWORDIGO_USE_DYNARMIC)
    if (MSVC)
        # MSVC: multi-config build produces the libs under a <Config>/ subdir.
        set(_dyn_cfg "$<UPPER_CASE:$<CONFIG>>")
        target_link_libraries(swordfare_boot PRIVATE
            ${SWORDIGO_DYNARMIC_ROOT}/src/dynarmic/$<UPPER_CASE:$<CONFIG>>/dynarmic.lib
            ${SWORDIGO_DYNARMIC_ROOT}/externals/mcl/src/$<UPPER_CASE:$<CONFIG>>/mcl.lib
            ${SWORDIGO_DYNARMIC_ROOT}/externals/fmt/$<UPPER_CASE:$<CONFIG>>/fmt.lib
            ${SWORDIGO_DYNARMIC_ROOT}/externals/zydis/$<UPPER_CASE:$<CONFIG>>/Zydis.lib
            ${SWORDIGO_DYNARMIC_ROOT}/externals/zydis/zycore/$<UPPER_CASE:$<CONFIG>>/Zycore.lib)
    else()
        target_link_libraries(swordfare_boot PRIVATE
            ${SWORDIGO_DYNARMIC_ROOT}/src/dynarmic/libdynarmic.a
            ${SWORDIGO_DYNARMIC_ROOT}/externals/mcl/src/libmcl.a
            ${SWORDIGO_DYNARMIC_ROOT}/externals/fmt/libfmt.a
            ${SWORDIGO_DYNARMIC_ROOT}/externals/zydis/libZydis.a
            ${SWORDIGO_DYNARMIC_ROOT}/externals/zydis/zycore/libZycore.a)
    endif()
endif()
