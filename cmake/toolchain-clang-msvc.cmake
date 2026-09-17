set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)

set(MSVC_BASE "/run/media/quantumcreeper/TVPG/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231")
set(WIN_SDK_INC "/run/media/quantumcreeper/TVPG/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0")
set(WIN_SDK_LIB "/run/media/quantumcreeper/TVPG/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0")

set(CMAKE_C_FLAGS_INIT "-target x86_64-pc-windows-msvc -fuse-ld=lld -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES /imsvc \"/home/quantumcreeper/.cache/winsdk_inc\" /imsvc \"${MSVC_BASE}/include\" /imsvc \"${WIN_SDK_INC}/ucrt\" /imsvc \"${WIN_SDK_INC}/shared\" /imsvc \"${WIN_SDK_INC}/um\"")
set(CMAKE_CXX_FLAGS_INIT "-target x86_64-pc-windows-msvc -fuse-ld=lld -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES /imsvc \"/home/quantumcreeper/.cache/winsdk_inc\" /imsvc \"${MSVC_BASE}/include\" /imsvc \"${WIN_SDK_INC}/ucrt\" /imsvc \"${WIN_SDK_INC}/shared\" /imsvc \"${WIN_SDK_INC}/um\"")

set(QT_MSVC_ROOT "/run/media/quantumcreeper/TVPG/vcpkg/6.12.0-0-202609141235qtbase-Windows_11")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-libpath:\"/home/quantumcreeper/.cache/winsdk_lib\" -libpath:\"${MSVC_BASE}/lib/x64\" -libpath:\"${WIN_SDK_LIB}/ucrt/x64\" -libpath:\"${WIN_SDK_LIB}/um/x64\" -libpath:\"/run/media/quantumcreeper/TVPG/vcpkg/installed/x64-windows/lib\" -libpath:\"${QT_MSVC_ROOT}/lib\"")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-libpath:\"/home/quantumcreeper/.cache/winsdk_lib\" -libpath:\"${MSVC_BASE}/lib/x64\" -libpath:\"${WIN_SDK_LIB}/ucrt/x64\" -libpath:\"${WIN_SDK_LIB}/um/x64\" -libpath:\"/run/media/quantumcreeper/TVPG/vcpkg/installed/x64-windows/lib\" -libpath:\"${QT_MSVC_ROOT}/lib\"")

list(APPEND CMAKE_PREFIX_PATH "${QT_MSVC_ROOT}" "${QT_MSVC_ROOT}/lib/cmake" "/run/media/quantumcreeper/TVPG/vcpkg/installed/x64-windows")
list(APPEND CMAKE_FIND_ROOT_PATH "${QT_MSVC_ROOT}" "/run/media/quantumcreeper/TVPG/vcpkg/installed/x64-windows")

set(QT_HOST_PATH "/usr" CACHE PATH "Host Qt path")
set(CMAKE_AUTOMOC_EXECUTABLE "/usr/lib64/qt6/libexec/moc" CACHE FILEPATH "Host moc")
set(CMAKE_AUTORCC_EXECUTABLE "/usr/lib64/qt6/libexec/rcc" CACHE FILEPATH "Host rcc")
set(CMAKE_AUTOUIC_EXECUTABLE "/usr/lib64/qt6/libexec/uic" CACHE FILEPATH "Host uic")
list(APPEND CMAKE_PROGRAM_PATH "/usr/lib64/qt6/libexec" "/usr/bin")
set(CMAKE_CROSSCOMPILING TRUE)
