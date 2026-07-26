cmake_minimum_required(VERSION 3.14)
project(cas_filesystem C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable compiler warnings & optimization flags
add_compile_options(-Wall -Wextra -O3 -g -D_FILE_OFFSET_BITS=64)

# Find Required Package Libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

# Include Directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${FUSE3_INCLUDE_DIRS}
)

# Source Files
set(SOURCES
    src/main.c
    src/crypto.c
    src/storage.c
    src/io_ring.c
    src/fuse_layer.c
)

# Executable Target
add_executable(cas_fs ${SOURCES})

# Link Libraries: libfuse3, liburing, liblmdb, libblake3, libcrypto
target_link_libraries(cas_fs PRIVATE
    ${FUSE3_LIBRARIES}
    uring
    lmdb
    blake3
    crypto
)
