# ============================================================================
# Component: third-party build steps (Dynarmic JIT, FFmpeg) + install rules.
# Dynarmic/FFmpeg are built out-of-tree via custom targets, then linked as
# static libs by swordfare_boot.
# ============================================================================

if (SWORDIGO_USE_DYNARMIC)
    add_custom_target(dynarmic-build
        COMMAND ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR}/deps/dynarmic -B ${SWORDIGO_DYNARMIC_ROOT} -DCMAKE_BUILD_TYPE=Release -DDYNARMIC_TESTS=OFF -DDYNARMIC_FRONTENDS=A64 -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DDYNARMIC_IGNORE_ASSERTS=ON -DDYNARMIC_WARNINGS_AS_ERRORS=OFF -Wno-dev
        COMMAND ${CMAKE_COMMAND} --build ${SWORDIGO_DYNARMIC_ROOT} --target dynarmic --config Release -j
        USES_TERMINAL)
endif()

# FFmpeg is dynamically loaded at runtime via dlopen (see platform/ffmpeg_dyn.cpp).
# No in-tree configure/build step is required.


# ---------------------------------------------------------------------------
# Install layout — the build drops products straight into the source tree:
#   bin/swordfare, bin/ruby, bin/ruby_cli  (executables)
#   bin/libs/*.so                          (component libs + libsre.so guest lib)
# These install rules only matter when staging into a packaging prefix.
# ---------------------------------------------------------------------------
if (SWORDIGO_BUILD_SRE)
    install(FILES "${CMAKE_SOURCE_DIR}/bin/libs/libsre12.so" DESTINATION bin/libs COMPONENT sre)
endif()
install(TARGETS swordfare_boot DESTINATION bin COMPONENT runtime)
if (SWORDIGO_BUILD_RUBY)
    install(TARGETS ruby ruby_cli DESTINATION bin COMPONENT runtime)
endif()
install(TARGETS swcore swgui swfmt swpod filerift swgfx swemu swordfare LIBRARY DESTINATION bin/libs COMPONENT runtime)
if (WIN32)
    install(TARGETS swcore swgui swfmt swpod filerift swgfx swemu swordfare RUNTIME DESTINATION bin COMPONENT runtime)
endif()
if (SWORDIGO_BUILD_RUBY_GG)
    install(TARGETS ruby_gg DESTINATION bin COMPONENT runtime)
endif()
