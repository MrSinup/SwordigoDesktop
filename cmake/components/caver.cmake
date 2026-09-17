# ============================================================================
# Component: caver — Ruby's own recovered Swordigo runtime.
#
# src/ruby/caver/ contains the engine as recovered from the decompilation
# (OpenSwordigo/arm64_12/functions/Caver/…), in caver:: :
#
#   component_registry.cpp — all 89 recovered component classes (81 serialised
#                            + 8 runtime-only) with their exact payload slots,
#                            taken from the binary's own kExtensionFieldNumber
#                            globals, plus per-class recovery stages.
#   runtime.cpp            — live object/component graph: identifiers, resolved
#                            *Id references, activation, spawning through the
#                            ObjectLibrary registry, the two-pass update
#                            (SCENE_OBJECT_UPDATE then PROGRAM_UPDATE) and the
#                            per-object render state the viewport consumes.
#   behaviour.cpp          — the recovered component behaviours (controllers,
#                            physics, animation, health/damage, triggers, doors,
#                            collectables, spells/projectiles, emitters).
#   visual.cpp             — components → draw items (data only; renders nowhere).
#   program_host.cpp       — the real Lua 5.1 VM: loads Program.Bytes exactly as
#                            the engine does and binds the engine API surface the
#                            shipped scripts call.
#   engine.cpp             — the preview engine: a worker thread + published
#                            snapshots, and the two backends (caver's recovered
#                            runtime vs. the host-owned live engine seam).
#   library_manager.cpp    — the ObjectLibrary registry (load / import / hot
#                            swap), i.e. the SCL runtime.
#
# Links filerift because the Program host reuses the embedded host Lua 5.1
# runtime that filerift already compiles (no second Lua, no libswordigo runner).
# ============================================================================

swordigo_library(caver
    ${SRC_DIR}/ruby/caver/component_registry.cpp
    ${SRC_DIR}/ruby/caver/runtime.cpp
    ${SRC_DIR}/ruby/caver/collision.cpp
    ${SRC_DIR}/ruby/caver/behaviour.cpp
    ${SRC_DIR}/ruby/caver/visual.cpp
    ${SRC_DIR}/ruby/caver/program_host.cpp
    ${SRC_DIR}/ruby/caver/engine.cpp
    ${SRC_DIR}/ruby/caver/library_manager.cpp)

target_link_libraries(caver PRIVATE
    swcore swfmt swpod filerift
    Threads::Threads ZLIB::ZLIB ${SWORDIGO_LIBM})
