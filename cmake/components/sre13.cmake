# ============================================================================
# Component: sre13 — ARM64 guest libsre13.so, cross-compiled with aarch64-linux-gnu.
# Produces bin/libs/libsre13.so directly for Swordigo 1.4.13.
#
# Layout:
#   src/sre/base   — shared infra (vendored lua, include/, sre_setjmp, ...)
#                    compiled INTO this library (no standalone base .so).
#   src/sre/sre13  — Swordigo 1.4.13 SRE code (sre13_*.c/h)
# ============================================================================

if (SWORDIGO_BUILD_SRE)
    find_program(AARCH64_CC aarch64-linux-gnu-gcc)
    if (AARCH64_CC)
        set(SRE_BASE_DIR ${SRC_DIR}/sre/base)
        set(SRE_V13_DIR ${SRC_DIR}/sre/sre13)
        set(SRE13_CORE_SRCS
            sre13_init
            sre13_lua
            sre13_rbmath
            sre13_recovery
            sre13_safety
            sre13_scene
            sre13_scene_shifter
            sre13_console
            sre13_audio
            sre13_ui
            sre13_extras_stubs
            core/hook
            core/stdstring
            core/map
            core/assets
            core/saves
            core/toml
            core/Gloss_compat
            hooks/BindingValue
            hooks/Program
            hooks/Camera
            hooks/CameraController
            hooks/CaverShell
            hooks/Component
            hooks/Component/CharControllerComponent
            hooks/Component/EntityComponent
            hooks/GameSceneController
            hooks/GameViewController
            hooks/ModelLibrary
            hooks/PhysicsObjectState
            hooks/PlayerProfile
            hooks/ProgramState
            hooks/RenderingContext
            hooks/Scene
            hooks/SceneObject
            hooks/TextureLibrary
            ui/sre13_button_controller
        )
        set(SRE13_SRCS)
        foreach(name IN LISTS LUA_SRCS)
            list(APPEND SRE13_SRCS ${SRE_BASE_DIR}/lua/src/${name}.c)
        endforeach()
        foreach(name IN LISTS SRE13_CORE_SRCS)
            list(APPEND SRE13_SRCS ${SRE_V13_DIR}/${name}.c)
        endforeach()
        list(APPEND SRE13_SRCS ${SRE_BASE_DIR}/sre_setjmp.S)

        set(SRE13_OBJECTS)
        foreach(source IN LISTS SRE13_SRCS)
            file(RELATIVE_PATH relative "${SRC_DIR}" "${source}")
            string(REPLACE "/" "_" object_name "${relative}")
            string(REGEX REPLACE "\\.(c|S)$" ".o" object_name "${object_name}")
            set(object "${CMAKE_BINARY_DIR}/sre13/${object_name}")
            add_custom_command(OUTPUT "${object}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/sre13"
                COMMAND ${AARCH64_CC} -shared -fPIC -O2 -nostdlib -fno-builtin -fno-stack-protector -I${SRE_BASE_DIR}/include -I${SRE_BASE_DIR} -I${SRE_V13_DIR} -I${SRE_V13_DIR}/core -I${SRE_V13_DIR}/caver -I${SRE_V13_DIR}/hooks -I${SRE_V13_DIR}/ui -I${SRE_BASE_DIR}/lua/src -c "${source}" -o "${object}"
                DEPENDS "${source}" VERBATIM)
            list(APPEND SRE13_OBJECTS "${object}")
        endforeach()
        add_custom_command(OUTPUT "${CMAKE_SOURCE_DIR}/bin/libs/libsre13.so"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/bin/libs"
            COMMAND ${AARCH64_CC} -shared -fPIC -nostdlib -o "${CMAKE_SOURCE_DIR}/bin/libs/libsre13.so" ${SRE13_OBJECTS}
            DEPENDS ${SRE13_OBJECTS} VERBATIM)
        add_custom_target(sre13 ALL DEPENDS "${CMAKE_SOURCE_DIR}/bin/libs/libsre13.so")
    endif()
endif()
