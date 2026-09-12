#!/usr/bin/env bash
# ==============================================================================
# package_dist_linux.sh
#
# Packages swordfare, ruby, ruby_gg and all required shared libraries into
# dist_linux/, strips all binaries & libraries to minimize size, creates
# portable launcher scripts, and creates a timestamped zip archive.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BIN_DIR="$ROOT_DIR/bin"
DIST_DIR="$ROOT_DIR/dist_linux"
DIST_LIBS_DIR="$DIST_DIR/libs"

TIMESTAMP="$(date +"%Y%m%d_%H%M%S")"
ZIP_NAME="dist_linux_${TIMESTAMP}.zip"
ZIP_PATH="$ROOT_DIR/$ZIP_NAME"

echo "========================================================================"
echo " Packaging Swordigo Desktop (Linux): swordfare, ruby, ruby_gg"
echo " Timestamp: $TIMESTAMP"
echo " Root:      $ROOT_DIR"
echo " Dest:      $DIST_DIR"
echo "========================================================================"

# 1. Prepare target directory
echo "[*] Cleaning and creating $DIST_DIR..."
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR" "$DIST_LIBS_DIR"

# 2. Copy core binaries
echo "[*] Copying core executables from bin/..."
for bin_name in swordfare ruby ruby_gg ruby_cli; do
    if [[ -f "$BIN_DIR/$bin_name" ]]; then
        echo "    -> Copying $bin_name"
        cp -p "$BIN_DIR/$bin_name" "$DIST_DIR/"
    else
        echo "    [!] Warning: $BIN_DIR/$bin_name not found!"
    fi
done

# Copy companion files for ruby_gg / Godot
if [[ -f "$BIN_DIR/ruby_gg.pck" ]]; then
    echo "    -> Copying ruby_gg.pck"
    cp -p "$BIN_DIR/ruby_gg.pck" "$DIST_DIR/"
fi

# 3. Copy required shared libraries
echo "[*] Copying shared libraries to dist_linux/libs/..."
if [[ -d "$BIN_DIR/libs" ]]; then
    cp -p "$BIN_DIR/libs"/*.so* "$DIST_LIBS_DIR/" 2>/dev/null || true
fi

# Also check dist/libs for any additional libraries (like libsre.so)
if [[ -d "$ROOT_DIR/dist/libs" ]]; then
    for so in "$ROOT_DIR/dist/libs"/*.so*; do
        if [[ -f "$so" ]]; then
            base_so="$(basename "$so")"
            if [[ ! -f "$DIST_LIBS_DIR/$base_so" ]]; then
                echo "    -> Copying extra library from dist/libs: $base_so"
                cp -p "$so" "$DIST_LIBS_DIR/"
            fi
        fi
    done
fi

echo "    Found $(ls -1 "$DIST_LIBS_DIR"/*.so* 2>/dev/null | wc -l) shared libraries in $DIST_LIBS_DIR"

# 4. Copy runtime data directory if available
if [[ -d "$ROOT_DIR/dist/data" ]]; then
    echo "[*] Copying runtime data/ assets from dist/data..."
    cp -a "$ROOT_DIR/dist/data" "$DIST_DIR/"
fi


# 5. Measure unstripped sizes
UNSTRIPPED_TOTAL_KB=$(du -sk "$DIST_DIR" | cut -f1)

# 6. Strip binaries and shared libraries
echo "[*] Stripping debug symbols from binaries and shared libraries..."
for elf in "$DIST_DIR"/*; do
    if [[ -f "$elf" && -x "$elf" && ! "$elf" =~ \.sh$ && ! "$elf" =~ \.pck$ ]]; then
        echo "    -> Stripping $(basename "$elf")"
        strip --strip-unneeded "$elf" 2>/dev/null || strip "$elf" 2>/dev/null || true
    fi
done

for so in "$DIST_LIBS_DIR"/*.so*; do
    if [[ -f "$so" ]]; then
        strip --strip-unneeded "$so" 2>/dev/null || strip "$so" 2>/dev/null || true
    fi
done

STRIPPED_TOTAL_KB=$(du -sk "$DIST_DIR" | cut -f1)
SAVED_KB=$((UNSTRIPPED_TOTAL_KB - STRIPPED_TOTAL_KB))

echo "[+] Size reduction: ${UNSTRIPPED_TOTAL_KB} KB -> ${STRIPPED_TOTAL_KB} KB (Saved ${SAVED_KB} KB)"

# 7. Create launcher scripts
echo "[*] Creating launcher scripts..."

# swordfare launcher
cat << 'EOF' > "$DIST_DIR/run_swordfare.sh"
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$DIR/libs:${LD_LIBRARY_PATH:-}"
export SWORDIGO_DATA_DIR="${SWORDIGO_DATA_DIR:-$DIR/data}"
exec "$DIR/swordfare" "$@"
EOF
chmod +x "$DIST_DIR/run_swordfare.sh"
ln -sf run_swordfare.sh "$DIST_DIR/run.sh"

# ruby launcher
cat << 'EOF' > "$DIST_DIR/run_ruby.sh"
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$DIR/libs:${LD_LIBRARY_PATH:-}"
export SWORDIGO_DATA_DIR="${SWORDIGO_DATA_DIR:-$DIR/data}"
exec "$DIR/ruby" "$@"
EOF
chmod +x "$DIST_DIR/run_ruby.sh"

# ruby_gg launcher
cat << 'EOF' > "$DIST_DIR/run_ruby_gg.sh"
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$DIR/libs:${LD_LIBRARY_PATH:-}"
export SWORDIGO_DATA_DIR="${SWORDIGO_DATA_DIR:-$DIR/data}"
exec "$DIR/ruby_gg" "$@"
EOF
chmod +x "$DIST_DIR/run_ruby_gg.sh"

# 8. Create timestamped zip archive
echo "[*] Creating timestamped zip archive: $ZIP_NAME..."
(cd "$ROOT_DIR" && zip -rq "$ZIP_NAME" dist_linux)

ZIP_SIZE_BYTES=$(stat -c%s "$ZIP_PATH")
ZIP_SIZE_MB=$(awk "BEGIN {printf \"%.2f\", $ZIP_SIZE_BYTES / 1048576}")

echo "========================================================================"
echo " [SUCCESS] Packaging complete!"
echo " Folder:  $DIST_DIR"
echo " Archive: $ZIP_PATH ($ZIP_SIZE_MB MB)"
echo "========================================================================"
