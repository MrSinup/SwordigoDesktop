# ============================================================================
# Component: sre12 — ARM64 guest libsre12.so (Swordigo 1.4.12 runtime),
# cross-compiled with aarch64-linux-gnu. Produces bin/libs/libsre12.so
# directly (the launcher loads it as guest code).
#
# Layout:
#   src/sre/base   — shared infra: vendored lua/raknet/toml-c/luasocket/lfs,
#                    fake-libc include/, sre_setjmp, sre_lua_compat, sre_base.c.
#                    Compiled INTO this library (no standalone base .so).
#   src/sre/sre12  — Swordigo 1.4.12 SRE code (all sre_*.c/h/S)
# ============================================================================

if (SWORDIGO_BUILD_SRE)
    find_program(AARCH64_CC aarch64-linux-gnu-gcc)
    if (NOT AARCH64_CC)
        message(WARNING "aarch64-linux-gnu-gcc not found — libsre.so will not be built. "
                        "Install the cross toolchain or disable SWORDIGO_BUILD_SRE.")
    else()
        set(SRE_BASE_DIR ${SRC_DIR}/sre/base)
        set(SRE_V12_DIR ${SRC_DIR}/sre/sre12)

        # NOTE: sre_ffi removed from core — FFI is now a closed-source feature
        # living in libsre-extras.so (src/sre/extras/sre_ffi.c).
        # Core only registers the safe stub _G.ffi surface via sre_extras_stubs.
        set(SRE_CORE_SRCS sre_init sre_string sre_lua sre_background sre_effects sre_music sre_gui sre_gui_native sre_scene_update sre_frame_loop sre_gui_nav sre_mini_api sre_vfs sre_lua_libs sre_pack_lua sre_raknet_c sre_mod sre_config sre_caver sre_features sre_scene_shifter sre_profile_panels sre_extras_stubs)
        set(SRE_SOCKET_SRCS auxiliar buffer except inet luasocket mime options select tcp timeout udp usocket)
        set(SRE_SRCS)
        foreach(name IN LISTS LUA_SRCS)
            list(APPEND SRE_SRCS ${SRE_BASE_DIR}/lua/src/${name}.c)
        endforeach()
        foreach(name IN LISTS SRE_CORE_SRCS)
            list(APPEND SRE_SRCS ${SRE_V12_DIR}/${name}.c)
        endforeach()
        list(APPEND SRE_SRCS ${SRE_BASE_DIR}/sre_base.c ${SRE_BASE_DIR}/sre_setjmp.S ${SRE_V12_DIR}/sre_scene_loading.S ${SRE_BASE_DIR}/toml-c/toml.c ${SRE_BASE_DIR}/luafilesystem/src/lfs.c)
        foreach(name IN LISTS SRE_SOCKET_SRCS)
            list(APPEND SRE_SRCS ${SRE_BASE_DIR}/luasocket/src/${name}.c)
        endforeach()
        set(SRE_OBJECTS)
        foreach(source IN LISTS SRE_SRCS)
            file(RELATIVE_PATH relative "${SRC_DIR}/sre" "${source}")
            string(REPLACE "/" "_" object_name "${relative}")
            string(REGEX REPLACE "\\.(c|S)$" ".o" object_name "${object_name}")
            set(object "${CMAKE_BINARY_DIR}/sre/${object_name}")
            set(extra_flags)
            if (NOT relative MATCHES "^base/lua/src/")
                list(APPEND extra_flags -I${SRE_BASE_DIR}/toml-c -I${SRE_BASE_DIR}/luasocket/src -I${SRE_BASE_DIR}/raknet -I${SRE_BASE_DIR} -I${SRE_V12_DIR} -include ${SRE_BASE_DIR}/sre_lua_compat.h)
            endif()
            add_custom_command(OUTPUT "${object}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/sre"
                COMMAND ${AARCH64_CC} -shared -fPIC -O2 -nostdlib -fno-builtin -fno-stack-protector -I${SRE_BASE_DIR}/include -I${SRE_BASE_DIR}/lua/src ${extra_flags} -c "${source}" -o "${object}"
                DEPENDS "${source}" ${SRE_BASE_DIR}/sre_lua_compat.h VERBATIM)
            list(APPEND SRE_OBJECTS "${object}")
        endforeach()
        add_custom_command(OUTPUT "${CMAKE_SOURCE_DIR}/bin/libs/libsre12.so"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/bin/libs"
            COMMAND ${AARCH64_CC} -shared -fPIC -nostdlib -o "${CMAKE_SOURCE_DIR}/bin/libs/libsre12.so" ${SRE_OBJECTS}
            DEPENDS ${SRE_OBJECTS} VERBATIM)
        add_custom_target(sre ALL DEPENDS "${CMAKE_SOURCE_DIR}/bin/libs/libsre12.so")
    endif()
endif()
