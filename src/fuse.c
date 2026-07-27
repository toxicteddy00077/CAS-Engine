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

static int read_chunk_data(const char *path, size_t chunk_idx, uint8_t *out, size_t *out_len, size_t file_size) {
    size_t chunk_start = chunk_idx * CAS_CHUNK_SIZE;
    if (chunk_start >= file_size) {
        *out_len = 0;
        return 0;
    }

    uint8_t hash[BLAKE3_HASH_LEN];
    if (cas_storage_get_file_chunk_hash(g_storage, path, chunk_idx, hash) != 0) {
        // Hole in file: return zeros for the part that exists
        size_t zero_len = file_size - chunk_start;
        if (zero_len > CAS_CHUNK_SIZE) zero_len = CAS_CHUNK_SIZE;
        memset(out, 0, zero_len);
        *out_len = zero_len;
        return 0;
    }

    uint8_t enc_buf[CAS_CHUNK_SIZE + 64];
    uint32_t enc_len = sizeof(enc_buf);
    if (cas_storage_get_chunk(g_storage, hash, enc_buf, &enc_len) != 0) return -EIO;

    uint32_t dec_len = 0;
    if (cas_crypto_decrypt(enc_buf, enc_len, out, &dec_len) != 0) return -EIO;

    size_t valid = dec_len;
    size_t max_valid = file_size - chunk_start;
    if (valid > max_valid) valid = max_valid;
    *out_len = valid;
    return 0;
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

    size_t start_chunk = (size_t)offset / CAS_CHUNK_SIZE;
    size_t end_chunk = ((size_t)offset + size - 1) / CAS_CHUNK_SIZE;
    size_t bytes_read = 0;

    for (size_t chunk_idx = start_chunk; chunk_idx <= end_chunk; chunk_idx++) {
        uint8_t dec_buf[CAS_CHUNK_SIZE];
        size_t dec_len = 0;
        if (read_chunk_data(path, chunk_idx, dec_buf, &dec_len, file_size) != 0) {
            return -EIO;
        }

        size_t chunk_start_offset = chunk_idx * CAS_CHUNK_SIZE;
        size_t copy_start = ((size_t)offset > chunk_start_offset) ? ((size_t)offset - chunk_start_offset) : 0;
        if (copy_start >= dec_len) continue;

        size_t bytes_to_copy = dec_len - copy_start;
        if (bytes_to_copy > (size - bytes_read)) bytes_to_copy = size - bytes_read;

        memcpy(buf + bytes_read, dec_buf + copy_start, bytes_to_copy);
        bytes_read += bytes_to_copy;
    }

    return (int)bytes_read;
}

static int write_chunk_data(const char *path, size_t chunk_idx, const uint8_t *data,
                            size_t data_offset_in_chunk, size_t data_len, size_t *new_file_size) {
    uint8_t chunk[CAS_CHUNK_SIZE];
    memset(chunk, 0, sizeof(chunk));

    size_t chunk_start = chunk_idx * CAS_CHUNK_SIZE;
    size_t existing_valid = 0;

    // Try to load existing chunk content
    uint8_t hash[BLAKE3_HASH_LEN];
    if (cas_storage_get_file_chunk_hash(g_storage, path, chunk_idx, hash) == 0) {
        uint8_t enc_buf[CAS_CHUNK_SIZE + 64];
        uint32_t enc_len = sizeof(enc_buf);
        if (cas_storage_get_chunk(g_storage, hash, enc_buf, &enc_len) == 0) {
            uint32_t dec_len = 0;
            if (cas_crypto_decrypt(enc_buf, enc_len, chunk, &dec_len) == 0) {
                existing_valid = dec_len;
            }
        }
    }

    // Apply new data
    if (data_offset_in_chunk + data_len > CAS_CHUNK_SIZE) {
        data_len = CAS_CHUNK_SIZE - data_offset_in_chunk;
    }
    memcpy(chunk + data_offset_in_chunk, data, data_len);

    size_t new_valid = existing_valid;
    if (data_offset_in_chunk + data_len > new_valid) {
        new_valid = data_offset_in_chunk + data_len;
    }

    uint8_t enc_buf[CAS_CHUNK_SIZE + 64];
    uint32_t enc_len = 0;
    if (cas_crypto_encrypt(chunk, new_valid, enc_buf, &enc_len) != 0) return -EIO;

    uint8_t new_hash[BLAKE3_HASH_LEN];
    if (cas_crypto_hash(enc_buf, enc_len, new_hash) != 0) return -EIO;

    if (cas_storage_put_chunk(g_storage, new_hash, enc_buf, enc_len) != 0) return -EIO;
    if (cas_storage_set_file_chunk_hash(g_storage, path, chunk_idx, new_hash) != 0) return -EIO;

    size_t chunk_file_end = chunk_start + new_valid;
    if (chunk_file_end > *new_file_size) *new_file_size = chunk_file_end;

    return 0;
}

static int cas_fuse_write(const char *path, const char *buf, size_t size, off_t offset,
                          struct fuse_file_info *fi) {
    (void) fi;

    size_t current_size = 0;
    bool is_dir = false;
    cas_storage_get_metadata(g_storage, path, &current_size, &is_dir);
    size_t new_file_size = current_size;

    size_t written = 0;
    while (written < size) {
        size_t current_offset = (size_t)offset + written;
        size_t chunk_idx = current_offset / CAS_CHUNK_SIZE;
        size_t offset_in_chunk = current_offset % CAS_CHUNK_SIZE;
        size_t remaining = size - written;
        size_t room_in_chunk = CAS_CHUNK_SIZE - offset_in_chunk;
        size_t chunk_bytes = (remaining > room_in_chunk) ? room_in_chunk : remaining;

        int rc = write_chunk_data(path, chunk_idx, (const uint8_t*)buf + written,
                                  offset_in_chunk, chunk_bytes, &new_file_size);
        if (rc != 0) return rc;

        written += chunk_bytes;
    }

    if (new_file_size > current_size) {
        cas_storage_put_metadata(g_storage, path, new_file_size, false);
    }

    return (int)size;
}

static int cas_fuse_unlink(const char *path) {
    return cas_storage_delete_metadata(g_storage, path);
}

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
    (void)argc;
    g_storage = storage;

    char *fuse_argv[4];
    fuse_argv[0] = argv[0];
    fuse_argv[1] = (char *)mountpoint;
    fuse_argv[2] = "-f";
    fuse_argv[3] = NULL;

    return fuse_main(3, fuse_argv, &cas_fuse_oper, NULL);
}
