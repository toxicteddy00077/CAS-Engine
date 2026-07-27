cmake_minimum_required(VERSION 3.14)
project(cas_filesystem C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable compiler warnings & optimization flags
add_compile_options(-Wall -Wextra -O3 -g -D_FILE_OFFSET_BITS=64)

# Find Required Package Libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
pkg_check_modules(LIBURING REQUIRED liburing)
pkg_check_modules(LMDB REQUIRED lmdb)
pkg_check_modules(LIBBLAKE3 REQUIRED libblake3)
pkg_check_modules(LIBSODIUM REQUIRED libsodium)

# Include Directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${FUSE3_INCLUDE_DIRS}
    ${LIBURING_INCLUDE_DIRS}
    ${LMDB_INCLUDE_DIRS}
    ${LIBBLAKE3_INCLUDE_DIRS}
    ${LIBSODIUM_INCLUDE_DIRS}
)

# Source Files
set(SOURCES
    src/driver.c
    src/engine.c
    src/fuse.c
    src/hash.c
    src/io_ring.c
    src/storage.c
    src/storage_api.c
)

# Executable Target
add_executable(cas_fs ${SOURCES})

# Link Libraries
target_link_libraries(cas_fs PRIVATE
    ${FUSE3_LIBRARIES}
    ${LIBURING_LIBRARIES}
    ${LMDB_LIBRARIES}
    ${LIBBLAKE3_LIBRARIES}
    ${LIBSODIUM_LIBRARIES}
    pthread
)
