cmake_minimum_required(VERSION 3.16)
project(libgit2_stripped C)

# Output library directly to bin/libs
if (NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${REPO_ROOT}/bin/libs")
endif()

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

set(LIBGIT2_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/util"
    "${CMAKE_CURRENT_SOURCE_DIR}/libgit2"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/xdiff"
)

# 1. Util sources
file(GLOB UTIL_SRCS
    "util/*.c"
    "util/allocators/*.c"
    "util/hash/*.c"
    "util/hash/sha1dc/*.c"
    "util/hash/rfc6234/*.c"
)

if (WIN32)
    file(GLOB UTIL_PLATFORM_SRCS "util/win32/*.c")
else()
    file(GLOB UTIL_PLATFORM_SRCS "util/unix/*.c")
endif()

# 2. XDiff sources
file(GLOB XDIFF_SRCS "deps/xdiff/*.c")

# 3. Libgit2 core sources
file(GLOB LIBGIT2_SRCS "libgit2/*.c")

set(ALL_SRCS ${UTIL_SRCS} ${UTIL_PLATFORM_SRCS} ${XDIFF_SRCS} ${LIBGIT2_SRCS})

add_library(git2 STATIC ${ALL_SRCS})

set_target_properties(git2 PROPERTIES
    OUTPUT_NAME "git2"
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(git2
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE ${LIBGIT2_INCLUDES}
)

target_compile_definitions(git2 PRIVATE
    _GNU_SOURCE
    _FILE_OFFSET_BITS=64
)

target_link_libraries(git2 PRIVATE
    ZLIB::ZLIB
    Threads::Threads
)

if (UNIX AND NOT APPLE)
    target_link_libraries(git2 PRIVATE rt)
endif()
