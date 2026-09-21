#!/usr/bin/env bash
# ============================================================================
# build_android.sh — Build script for Ruby GG Mobile Android Port
#   Exclusively utilizes Android build tools directly from the TVPG drive:
#   /run/media/quantumcreeper/TVPG/linuxFiles/Applications/AndroidBuildTools
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

# TVPG Android Build Tools Root
ANDROID_SDK_ROOT="/run/media/quantumcreeper/TVPG/linuxFiles/Applications/AndroidBuildTools"
ANDROID_NDK_ROOT="${ANDROID_SDK_ROOT}/ndk/28.2.13676358"
BUILD_TOOLS_DIR="${ANDROID_SDK_ROOT}/build-tools/35.0.0"
PLATFORM_DIR="${ANDROID_SDK_ROOT}/platforms/android-36"
ANDROID_JAR="${PLATFORM_DIR}/android.jar"
TOOLCHAIN_FILE="${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake"

AAPT2="${BUILD_TOOLS_DIR}/aapt2"
D8="${BUILD_TOOLS_DIR}/d8"
ZIPALIGN="${BUILD_TOOLS_DIR}/zipalign"
APKSIGNER="${BUILD_TOOLS_DIR}/apksigner"
ADB="${ANDROID_SDK_ROOT}/platform-tools/adb"
STRIP="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"

echo "=== Ruby GG Mobile Android Build System ==="
echo "SDK:        ${ANDROID_SDK_ROOT}"
echo "NDK:        ${ANDROID_NDK_ROOT}"
echo "BuildTools: ${BUILD_TOOLS_DIR}"
echo "Platform:   android-36"

# Supported ABIs (arm64-v8a is primary, with armeabi-v7a and x86_64 support)
ABIS=("arm64-v8a")
if [[ "${1:-}" == "--all-abis" ]]; then
    ABIS=("arm64-v8a" "armeabi-v7a" "x86_64")
fi

BUILD_ROOT="${PROJECT_ROOT}/build-android"
mkdir -p "${BUILD_ROOT}"

for ABI in "${ABIS[@]}"; do
    echo ""
    echo "--- Building native shared library for ${ABI} ---"
    ABI_BUILD_DIR="${BUILD_ROOT}/${ABI}"
    mkdir -p "${ABI_BUILD_DIR}"

    QT_ABI_DIR="${BUILD_ROOT}/qt6/6.6.3/android_${ABI//-/_}"
    if [ ! -d "${QT_ABI_DIR}" ]; then
        if [ -n "${QT_DIR:-}" ] && [ -d "${QT_DIR}" ]; then
            QT_ABI_DIR="${QT_DIR}"
        elif [ -d "/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_${ABI//-/_}" ]; then
            QT_ABI_DIR="/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_${ABI//-/_}"
        elif [ -d "/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_arm64_v8a" ]; then
            QT_ABI_DIR="/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_arm64_v8a"
        else
            QT_ABI_DIR="${BUILD_ROOT}/qt6/6.6.3/android_arm64_v8a"
        fi
    fi

    QT_HOST_DIR="${BUILD_ROOT}/qt6/6.6.3/gcc_64"
    if [ ! -d "${QT_HOST_DIR}" ] && [ -d "/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/gcc_64" ]; then
        QT_HOST_DIR="/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/gcc_64"
    fi

    cmake -S "${SCRIPT_DIR}" -B "${ABI_BUILD_DIR}" \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
        -DANDROID_ABI="${ABI}" \
        -DANDROID_PLATFORM=android-24 \
        -DCMAKE_BUILD_TYPE=Release \
        -DANDROID_STL=c++_shared \
        -DCMAKE_FIND_ROOT_PATH="${QT_ABI_DIR}" \
        -DCMAKE_PREFIX_PATH="${QT_ABI_DIR}" \
        -DQT_HOST_PATH="${QT_HOST_DIR}" \
        -DQt6_DIR="${QT_ABI_DIR}/lib/cmake/Qt6"

    cmake --build "${ABI_BUILD_DIR}" --config Release -j"$(nproc)"
    echo "[✓] Native library built: ${ABI_BUILD_DIR}/libruby.so"
done

echo ""
echo "=== Packaging APK ==="
PACKAGE_DIR="${BUILD_ROOT}/apk_staging"
rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}/lib" "${PACKAGE_DIR}/res" "${PACKAGE_DIR}/assets"

# Copy native libraries for each ABI
for ABI in "${ABIS[@]}"; do
    mkdir -p "${PACKAGE_DIR}/lib/${ABI}"
    
    # 1. Main application library (both libruby.so and libruby_${ABI}.so for QtLoader compatibility)
    cp "${BUILD_ROOT}/${ABI}/libruby.so" "${PACKAGE_DIR}/lib/${ABI}/libruby.so"
    cp "${BUILD_ROOT}/${ABI}/libruby.so" "${PACKAGE_DIR}/lib/${ABI}/libruby_${ABI}.so"

    # 2. NDK libc++_shared.so
    LIBCXX="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib"
    if [[ "${ABI}" == "arm64-v8a" ]]; then
        cp "${LIBCXX}/aarch64-linux-android/libc++_shared.so" "${PACKAGE_DIR}/lib/${ABI}/"
    elif [[ "${ABI}" == "armeabi-v7a" ]]; then
        cp "${LIBCXX}/arm-linux-androideabi/libc++_shared.so" "${PACKAGE_DIR}/lib/${ABI}/"
    elif [[ "${ABI}" == "x86_64" ]]; then
        cp "${LIBCXX}/x86_64-linux-android/libc++_shared.so" "${PACKAGE_DIR}/lib/${ABI}/"
    fi

    # 3. Qt shared libraries and plugins for this ABI
    QT_ABI_DIR="${BUILD_ROOT}/qt6/6.6.3/android_${ABI//-/_}"
    if [ ! -d "${QT_ABI_DIR}" ]; then
        if [ -n "${QT_DIR:-}" ] && [ -d "${QT_DIR}" ]; then
            QT_ABI_DIR="${QT_DIR}"
        elif [ -d "/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_${ABI//-/_}" ]; then
            QT_ABI_DIR="/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_${ABI//-/_}"
        elif [ -d "/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_arm64_v8a" ]; then
            QT_ABI_DIR="/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_arm64_v8a"
        else
            QT_ABI_DIR="${BUILD_ROOT}/qt6/6.6.3/android_arm64_v8a"
        fi
    fi

    echo "Copying Qt shared libraries from ${QT_ABI_DIR}/lib..."
    if [ -d "${QT_ABI_DIR}/lib" ]; then
        cp "${QT_ABI_DIR}/lib"/libQt6*.so "${PACKAGE_DIR}/lib/${ABI}/"
    fi

    echo "Copying Qt plugins from ${QT_ABI_DIR}/plugins..."
    if [ -d "${QT_ABI_DIR}/plugins" ]; then
        find "${QT_ABI_DIR}/plugins" -name "*.so" -exec cp {} "${PACKAGE_DIR}/lib/${ABI}/" \;
    fi

    echo "Copying Qt QML plugins from ${QT_ABI_DIR}/qml..."
    if [ -d "${QT_ABI_DIR}/qml" ]; then
        find "${QT_ABI_DIR}/qml" -name "*.so" -exec cp {} "${PACKAGE_DIR}/lib/${ABI}/" \;
    fi

    # 4. Strip unneeded debug symbols from native shared libraries for release
    echo "Stripping debug symbols from ${PACKAGE_DIR}/lib/${ABI}..."
    if [ -x "${STRIP}" ]; then
        find "${PACKAGE_DIR}/lib/${ABI}" -name "*.so" -exec "${STRIP}" --strip-unneeded {} + 2>/dev/null || true
    fi
done

# Package QML assets into assets/qml
echo "Packaging QML modules into assets/qml..."
mkdir -p "${PACKAGE_DIR}/assets/qml"
QT_QML_DIR="${BUILD_ROOT}/qt6/6.6.3/android_arm64_v8a/qml"
if [ ! -d "${QT_QML_DIR}" ] && [ -d "/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_arm64_v8a/qml" ]; then
    QT_QML_DIR="/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_arm64_v8a/qml"
fi
if [ -d "${QT_QML_DIR}" ]; then
    cp -r "${QT_QML_DIR}/"* "${PACKAGE_DIR}/assets/qml/"
    find "${PACKAGE_DIR}/assets/qml" -name "*.so" -delete
fi

# Compile resources using aapt2
"${AAPT2}" compile --dir "${SCRIPT_DIR}/res" -o "${BUILD_ROOT}/compiled_res.zip"
"${AAPT2}" link -o "${BUILD_ROOT}/unaligned.apk" \
    -I "${ANDROID_JAR}" \
    --manifest "${SCRIPT_DIR}/AndroidManifest.xml" \
    -A "${PACKAGE_DIR}/assets" \
    "${BUILD_ROOT}/compiled_res.zip" \
    --auto-add-overlay

# Compile Java sources
JAVA_OUT="${BUILD_ROOT}/java_classes"
rm -rf "${JAVA_OUT}"
mkdir -p "${JAVA_OUT}"

QT_JAR_DIR="${BUILD_ROOT}/qt6/6.6.3/android_arm64_v8a/jar"
QT_SRC_DIR="${BUILD_ROOT}/qt6/6.6.3/android_arm64_v8a/src/android/java/src"
if [ ! -d "${QT_JAR_DIR}" ] && [ -d "/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_arm64_v8a/jar" ]; then
    QT_JAR_DIR="/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_arm64_v8a/jar"
    QT_SRC_DIR="/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_arm64_v8a/src/android/java/src"
fi

QT_CP=""
for j in "${QT_JAR_DIR}"/*.jar; do
    if [ -f "${j}" ]; then
        QT_CP="${QT_CP}:${j}"
    fi
done

JAVA_SRCS=("${SCRIPT_DIR}/java/in/aevora/ruby/RubyActivity.java")
while IFS= read -r -d '' src; do
    JAVA_SRCS+=("${src}")
done < <(find "${QT_SRC_DIR}" -name "*.java" -print0)

javac -source 11 -target 11 -cp "${ANDROID_JAR}${QT_CP}" \
    -d "${JAVA_OUT}" \
    "${JAVA_SRCS[@]}"

# Dex classes
DEX_DIR="${BUILD_ROOT}/dex"
mkdir -p "${DEX_DIR}"
CLASS_FILES=()
while IFS= read -r -d '' cls; do
    CLASS_FILES+=("${cls}")
done < <(find "${JAVA_OUT}" -name "*.class" -print0)

"${D8}" --output "${DEX_DIR}" --lib "${ANDROID_JAR}" "${CLASS_FILES[@]}" "${QT_JAR_DIR}"/*.jar

# Add classes.dex to APK
(cd "${DEX_DIR}" && zip -u "${BUILD_ROOT}/unaligned.apk" classes*.dex)

# Add native libraries and assets to APK
(cd "${PACKAGE_DIR}" && zip -r -u "${BUILD_ROOT}/unaligned.apk" lib assets)

# Align APK
FINAL_APK="${PROJECT_ROOT}/bin/ruby_gg_mobile.apk"
mkdir -p "${PROJECT_ROOT}/bin"
"${ZIPALIGN}" -f 4 "${BUILD_ROOT}/unaligned.apk" "${FINAL_APK}"

# Debug signing if keystore available or generate ephemeral debug key
KEYSTORE="${BUILD_ROOT}/debug.keystore"
if [ ! -f "${KEYSTORE}" ]; then
    keytool -genkey -v -keystore "${KEYSTORE}" -storepass android -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
fi
"${APKSIGNER}" sign --ks "${KEYSTORE}" --ks-pass pass:android --ks-key-alias androiddebugkey --key-pass pass:android "${FINAL_APK}"

echo "[✓] Successfully built APK: ${FINAL_APK}"

# Check for ADB device
if "${ADB}" devices | grep -q -E "[a-zA-Z0-9_-]+\s+device$"; then
    echo "Found connected ADB device. Installing..."
    "${ADB}" install -r "${FINAL_APK}"
    echo "[✓] Installed ruby_gg_mobile to device!"
    echo "Launching in.aevora.ruby..."
    "${ADB}" shell am start -n in.aevora.ruby/.RubyActivity || true
else
    echo "NOTE: No ADB device in 'device' state currently. Ensure USB debugging is ON."
fi
