#!/usr/bin/env bash
# =============================================================================
# build_swordfare.sh — Consumer Build Launcher for SwordigoDesktop (Linux)
#
# Default: Boots the native Qt6 SwordigoDesktop Build Studio GUI.
# CLI Mode: Pass --nogui to compile directly from the terminal.
#
# Note: Developer workflows (run_swordigo.sh, run_openswordigo.sh) remain
# untouched and should be used by developers for unstripped daily development.
# =============================================================================
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build-cmake"

NOGUI=0
CLEAN=0
STRIP=0
USE_DYNARMIC=1
HOST_COMPILER="gcc"
SRE_COMPILER="gcc"
TARGETS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --nogui)
            NOGUI=1
            shift
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        --strip)
            STRIP=1
            shift
            ;;
        --no-dynarmic)
            USE_DYNARMIC=0
            shift
            ;;
        --compiler-host)
            HOST_COMPILER="$2"
            shift 2
            ;;
        --compiler-sre)
            SRE_COMPILER="$2"
            shift 2
            ;;
        --target)
            TARGETS+=("$2")
            shift 2
            ;;
        --help|-h)
            echo "Usage: ./build_swordfare.sh [options]"
            echo ""
            echo "Options:"
            echo "  --nogui                 Run CLI build without launching Qt Build Studio"
            echo "  --target <name>         Build specific CMake target(s) (can specify multiple)"
            echo "  --compiler-host <gcc|clang> Host compiler (default: gcc)"
            echo "  --compiler-sre <gcc>    SRE cross-compiler (default: gcc)"
            echo "  --strip                 Strip binaries for smaller distribution size"
            echo "  --no-dynarmic           Disable Dynarmic ARM64 JIT"
            echo "  --clean                 Wipe build-cmake before building"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

# ─── 1. GUI Mode (Default) ──────────────────────────────────────────────────
if [ "$NOGUI" -eq 0 ]; then
    STUDIO_BIN="$ROOT_DIR/bin/swordigo_build_studio"

    if [ ! -x "$STUDIO_BIN" ]; then
        echo "=================================================="
        echo "  Bootstrapping SwordigoDesktop Build Studio...   "
        echo "=================================================="
        cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DSWORDIGO_BUILD_STUDIO=ON
        cmake --build "$BUILD_DIR" --target swordigo_build_studio -j"$(nproc)"
    fi

    if [ -x "$STUDIO_BIN" ]; then
        exec "$STUDIO_BIN" --project-dir "$ROOT_DIR"
    else
        echo "Failed to start Build Studio. Falling back to --nogui build." >&2
    fi
fi

# ─── 2. CLI Mode (--nogui) ───────────────────────────────────────────────────
echo "=================================================="
echo "  SwordigoDesktop — Consumer Build Engine (CLI)   "
echo "=================================================="

if [ "$CLEAN" -eq 1 ]; then
    echo "[Clean] Removing $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
fi

CMAKE_FLAGS=(
    -S "$ROOT_DIR"
    -B "$BUILD_DIR"
    -DSWORDIGO_USE_DYNARMIC="$USE_DYNARMIC"
    -DSWORDIGO_BUILD_STUDIO=ON
)

if [ "$STRIP" -eq 1 ]; then
    CMAKE_FLAGS+=(-DSWORDIGO_STRIP_RELEASE=ON)
else
    CMAKE_FLAGS+=(-DSWORDIGO_STRIP_RELEASE=OFF)
fi

# Host compiler configuration
if [ "$HOST_COMPILER" = "clang" ]; then
    CMAKE_FLAGS+=(
        -DCMAKE_C_COMPILER=clang
        -DCMAKE_CXX_COMPILER=clang++
    )
fi

# SRE guest cross-compiler (GCC default; Clang locked until testing)
if [ "$SRE_COMPILER" = "gcc" ]; then
    CMAKE_FLAGS+=(-DSWORDIGO_BUILD_SRE=ON)
fi

echo "[Configure] Running CMake..."
cmake "${CMAKE_FLAGS[@]}"

# If no targets specified, default to full game
if [ ${#TARGETS[@]} -eq 0 ]; then
    TARGETS=("dynarmic-build" "sre" "swordfare")
fi

echo "[Build] Compiling target(s): ${TARGETS[*]}..."
for target in "${TARGETS[@]}"; do
    if [ "$target" = "dynarmic-build" ] && [ "$USE_DYNARMIC" -eq 1 ]; then
        if [ ! -f "$ROOT_DIR/deps/dynarmic/build/src/dynarmic/libdynarmic.a" ]; then
            cmake --build "$BUILD_DIR" --target dynarmic-build -j "$(nproc)"
        fi
    else
        cmake --build "$BUILD_DIR" --target "$target" -j "$(nproc)"
    fi
done

echo ""
echo "✓ Build completed successfully!"
echo "Binaries are located in: $ROOT_DIR/bin/"
