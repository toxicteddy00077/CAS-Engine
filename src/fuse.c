#include "../include/types.h"
#include "../include/storage.h"
#include "../include/engine.h"
#include <lmdb.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#define FUSE_USE_VERSION 30
#include <fuse3/fuse.h>

static int cas_fuse_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    // Query LMDB for virtual inode/metadata
    size_t file_size = 0;
    bool is_dir = false;
    if (cas_storage_get_metadata(g_storage, path, &file_size, &is_dir) == 0) {
        if (is_dir) {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
        } else {
            stbuf->st_mode = S_IFREG | 0644;
            stbuf->st_nlink = 1;
            stbuf->st_size = file_size;
        }
        return 0;
    }

    return -ENOENT;
}

static int cas_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi,
                            enum fuse_readdir_flags flags) {
    (void) offset; (void) fi; (void) flags;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    // List all files and subdirectories under 'path' from LMDB metadata
    cas_storage_list_dir(g_storage, path, buf, filler);
    return 0;
}

static int cas_fuse_mkdir(const char *path, mode_t mode) {
    (void) mode;
    return cas_storage_put_metadata(g_storage, path, 0, true);
}

static int cas_fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) mode; (void) fi;
    return cas_storage_put_metadata(g_storage, path, 0, false);
}


static int cas_fuse_read(const char *path, char *buf, size_t size, off_t offset,
                         struct fuse_file_info *fi) {
    (void) fi;

    size_t file_size = 0;
    bool is_dir = false;
    if (cas_storage_get_metadata(g_storage, path, &file_size, &is_dir) != 0 || is_dir) {
        return -ENOENT;
    }

    if (offset >= (off_t)file_size) return 0;
    if (offset + size > file_size) size = file_size - offset;

    // Determine which chunks overlap with [offset, offset + size]
    size_t start_chunk = offset / CAS_CHUNK_SIZE;
    size_t end_chunk = (offset + size - 1) / CAS_CHUNK_SIZE;
    size_t bytes_read = 0;

    for (size_t chunk_idx = start_chunk; chunk_idx <= end_chunk; chunk_idx++) {
        uint8_t hash[32];
        if (cas_storage_get_file_chunk_hash(g_storage, path, chunk_idx, hash) != 0) {
            break;
        }

        uint8_t enc_buf[CAS_CHUNK_SIZE + 64];
        uint32_t enc_len = sizeof(enc_buf);

        if (cas_storage_get_chunk(g_storage, hash, enc_buf, &enc_len) != 0) {
            return -EIO;
        }

        uint8_t dec_buf[CAS_CHUNK_SIZE];
        uint32_t dec_len = 0;
        if (cas_crypto_decrypt(enc_buf, enc_len, dec_buf, &dec_len) != 0) {
            return -EIO;
        }

        // Calculate slice to copy into output buffer
        size_t chunk_start_offset = chunk_idx * CAS_CHUNK_SIZE;
        size_t copy_start = (offset > (off_t)chunk_start_offset) ? (offset - chunk_start_offset) : 0;
        size_t bytes_to_copy = dec_len - copy_start;
        if (bytes_to_copy > (size - bytes_read)) {
            bytes_to_copy = size - bytes_read;
        }

        memcpy(buf + bytes_read, dec_buf + copy_start, bytes_to_copy);
        bytes_read += bytes_to_copy;
    }

    return (int)bytes_read;
}

// ---------------------------------------------------------------------------
// 6. WRITE (Chunk incoming data, hash, encrypt, and commit to CAS)
// ---------------------------------------------------------------------------
static int cas_fuse_write(const char *path, const char *buf, size_t size, off_t offset,
                          struct fuse_file_info *fi) {
    (void) fi;

    size_t written = 0;
    while (written < size) {
        size_t current_offset = offset + written;
        size_t chunk_idx = current_offset / CAS_CHUNK_SIZE;
        size_t chunk_bytes = size - written;
        if (chunk_bytes > CAS_CHUNK_SIZE) chunk_bytes = CAS_CHUNK_SIZE;

        // Encrypt payload
        uint8_t enc_buf[CAS_CHUNK_SIZE + 64];
        uint32_t enc_len = 0;
        if (cas_crypto_encrypt((const uint8_t*)buf + written, chunk_bytes, enc_buf, &enc_len) != 0) {
            return -EIO;
        }

        // Compute BLAKE3 content hash address
        uint8_t hash[32];
        cas_crypto_hash(enc_buf, enc_len, hash);

        // Deduplicated Storage Put (via io_ring)
        if (cas_storage_put_chunk(g_storage, hash, enc_buf, enc_len) != 0) {
            return -EIO;
        }

        // Link chunk hash index to path in LMDB
        cas_storage_set_file_chunk_hash(g_storage, path, chunk_idx, hash);

        written += chunk_bytes;
    }

    // Update total virtual file size in LMDB
    size_t current_size = 0;
    bool is_dir = false;
    cas_storage_get_metadata(g_storage, path, &current_size, &is_dir);
    if (offset + size > current_size) {
        cas_storage_put_metadata(g_storage, path, offset + size, false);
    }

    return (int)size;
}

static int cas_fuse_unlink(const char *path) {
    return cas_storage_delete_metadata(g_storage, path);
}

// FUSE operations function table
static const struct fuse_operations cas_fuse_oper = {
    .getattr = cas_fuse_getattr,
    .readdir = cas_fuse_readdir,
    .mkdir   = cas_fuse_mkdir,
    .create  = cas_fuse_create,
    .read    = cas_fuse_read,
    .write   = cas_fuse_write,
    .unlink  = cas_fuse_unlink,
};

int cas_fuse_mount(const char *mountpoint, cas_storage_t *storage, int argc, char *argv[]) {
    g_storage = storage;

    // Prepare FUSE command line arguments
    char *fuse_argv[4];
    fuse_argv[0] = argv[0];
    fuse_argv[1] = (char *)mountpoint;
    fuse_argv[2] = "-f"; // Keep in foreground for debugging
    fuse_argv[3] = NULL;

    return fuse_main(3, fuse_argv, &cas_fuse_oper, NULL);
}
