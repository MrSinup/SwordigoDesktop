#!/usr/bin/env bash
# =============================================================================
# dist_ruby_win64.sh — Standalone stripped Windows release build for Ruby
# =============================================================================
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-win64"
DIST_DIR="$PROJECT_ROOT/dist_ruby_win64"
VCPKG_BIN="/run/media/quantumcreeper/TVPG/vcpkg/installed/x64-mingw-dynamic/bin"
MINGW_BIN="/usr/x86_64-w64-mingw32/sys-root/mingw/bin"

echo "=================================================="
echo "  Ruby (Windows x64) — Stripped Distribution Build"
echo "=================================================="
echo ""

# --- Step 1: Configure CMake with MinGW toolchain ---
echo "[1/6] Configuring CMake for MinGW-w64 Windows x64..."
cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT" \
    -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/toolchain-mingw64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSWORDIGO_STRIP_RELEASE=ON \
    -DSWORDIGO_STATIC=ON \
    -DSWORDIGO_USE_DYNARMIC=OFF \
    -DSWORDIGO_USE_FFMPEG=OFF \
    -DSWORDIGO_BUILD_SRE=OFF \
    -DSWORDIGO_BUILD_RUBY=ON \
    -DSWORDIGO_BUILD_RUBY_GG=OFF \
    -DBUILD_TESTING=OFF

echo ""

# --- Step 2: Build ruby and ruby_cli ---
echo "[2/6] Building ruby target(s)..."
cmake --build "$BUILD_DIR" --target ruby ruby_cli -j"$(nproc)"

echo ""

# --- Step 3: Prepare clean dist directory ---
echo "[3/6] Preparing distribution directory: $DIST_DIR..."
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

# --- Step 4: Copy & strip executables ---
echo "[4/6] Copying & stripping executables..."
for exe in ruby.exe ruby_cli.exe; do
    src="$PROJECT_ROOT/binw/$exe"
    if [ -f "$src" ]; then
        cp "$src" "$DIST_DIR/$exe"
        x86_64-w64-mingw32-strip --strip-all "$DIST_DIR/$exe" 2>/dev/null || strip "$DIST_DIR/$exe" 2>/dev/null || true
        echo "  ✓ $exe ($(du -h "$DIST_DIR/$exe" | cut -f1))"
    else
        echo "  ⚠ $exe not found in binw/"
    fi
done

echo ""

# --- Step 5: Stage runtime DLLs ---
echo "[5/6] Staging runtime DLLs..."

# vcpkg dependencies
VCPKG_DLLS=(
    SDL3.dll
    SDL3_image.dll
    glew32.dll
    OpenAL32.dll
    libz.dll
    libfmt.dll
)

for dll in "${VCPKG_DLLS[@]}"; do
    if [ -f "$VCPKG_BIN/$dll" ]; then
        cp "$VCPKG_BIN/$dll" "$DIST_DIR/$dll"
        x86_64-w64-mingw32-strip --strip-unneeded "$DIST_DIR/$dll" 2>/dev/null || true
        echo "  ✓ $dll (vcpkg)"
    fi
done

# MinGW runtime DLLs (GCC / C++ / WinThreads)
MINGW_DLLS=(
    libgcc_s_seh-1.dll
    libstdc++-6.dll
    libwinpthread-1.dll
)

for dll in "${MINGW_DLLS[@]}"; do
    if [ -f "$MINGW_BIN/$dll" ]; then
        cp "$MINGW_BIN/$dll" "$DIST_DIR/$dll"
        x86_64-w64-mingw32-strip --strip-unneeded "$DIST_DIR/$dll" 2>/dev/null || true
        echo "  ✓ $dll (MinGW runtime)"
    fi
done

# Copy any bundled fonts or shaders if available
if [ -d "$PROJECT_ROOT/src/assets/fonts" ]; then
    mkdir -p "$DIST_DIR/fonts"
    cp -r "$PROJECT_ROOT/src/assets/fonts/"* "$DIST_DIR/fonts/" 2>/dev/null || true
fi

echo ""

# --- Step 6: Create distribution zip package ---
echo "[6/6] Packaging into ruby-win64.zip..."
rm -f "$PROJECT_ROOT/ruby-win64.zip"
(cd "$DIST_DIR" && zip -r9 "$PROJECT_ROOT/ruby-win64.zip" .)

echo ""
echo "=================================================="
echo "  ✓ Ruby Windows Distribution Build Complete!"
echo "=================================================="
echo "Output directory: $DIST_DIR"
echo "Output archive:   $PROJECT_ROOT/ruby-win64.zip ($(du -h "$PROJECT_ROOT/ruby-win64.zip" 2>/dev/null | cut -f1))"
