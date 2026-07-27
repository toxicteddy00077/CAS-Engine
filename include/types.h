#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <lmdb.h>
#include <liburing.h>

#define BLAKE3_HASH_LEN 32
#define NONCE_LEN 24
#define MAX_SEGMENT_SIZE (1024 * 1024 * 1024) // 1 GB Segment files
#define PATH_MAX_LEN 4096
#define CAS_CHUNK_SIZE (64 * 1024) // 64 KB chunks for CAS

typedef struct {
    uint8_t bytes[BLAKE3_HASH_LEN];
} cas_hash_t;


typedef struct{
    const char* storage_dir;
    const char* mount_point;
    size_t chunk_size;
} cas_config_t;

typedef struct{
    uint32_t seg_id; // blob identifier
    uint64_t offset; // offset in the blob
    uint32_t len;
} cas_loc_t;

typedef struct{
    MDB_env *env;
    MDB_dbi dbi_chunks;

    char *store_dir;
    uint32_t active_seg_id; // ID of current active blob file (e.g., 1, 2, 3)
    int      active_fd;         // File descriptor of open segment file
    uint64_t active_offset;     // Byte offset where the NEXT write will happen
    uint64_t seg_size;
} cas_storage_t;

typedef struct{
    struct io_uring ring;
    uint32_t q_depth;
    uint32_t pending_sqe;
} cas_io_ring;

extern cas_storage_t *g_storage;

#endif
