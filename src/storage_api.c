#include "../include/types.h"
#include "../include/storage.h"

#include <lmdb.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#define FUSE_USE_VERSION 30
#include <fuse3/fuse.h>

static int put_meta(cas_storage_t *s, const char *path, size_t size, bool is_dir, bool overwrite) {
    if (!s || !path) return -1;
    MDB_txn *txn;
    if (mdb_txn_begin(s->env, NULL, 0, &txn) != 0) return -1;

    char kbuf[PATH_MAX_LEN + 8];
    size_t plen = strlen(path);
    if (plen + 1 > sizeof(kbuf)) { mdb_txn_abort(txn); return -1; }
    memcpy(kbuf, path, plen + 1);

    MDB_val key = { .mv_size = plen + 1, .mv_data = kbuf };

    struct { size_t size; uint8_t is_dir; } val = { .size = size, .is_dir = is_dir ? 1 : 0 };
    MDB_val mdbval = { .mv_size = sizeof(val), .mv_data = &val };

    int rc = mdb_put(txn, s->dbi_chunks, &key, &mdbval, overwrite ? 0 : MDB_NOOVERWRITE);
    if (rc == MDB_KEYEXIST) { mdb_txn_commit(txn); return 0; }
    if (rc != 0) { mdb_txn_abort(txn); return -1; }
    if (mdb_txn_commit(txn) != 0) return -1;
    return 0;
}

int cas_storage_put_metadata(cas_storage_t *s, const char *path, size_t size, bool is_dir) {
    return put_meta(s, path, size, is_dir, true);
}

int cas_storage_get_metadata(cas_storage_t *s, const char *path, size_t *out_size, bool *out_is_dir) {
    if (!s || !path) return -1;
    MDB_txn *txn;
    if (mdb_txn_begin(s->env, NULL, MDB_RDONLY, &txn) != 0) return -1;

    MDB_val key = { .mv_size = strlen(path) + 1, .mv_data = (void*)path };
    MDB_val mdbval;
    int rc = mdb_get(txn, s->dbi_chunks, &key, &mdbval);
    if (rc != 0) { mdb_txn_abort(txn); return -1; }

    if (mdbval.mv_size < sizeof(size_t) + 1) { mdb_txn_abort(txn); return -1; }
    struct { size_t size; uint8_t is_dir; } *v = mdbval.mv_data;
    if (out_size) *out_size = v->size;
    if (out_is_dir) *out_is_dir = v->is_dir != 0;
    mdb_txn_abort(txn);
    return 0;
}

int cas_storage_delete_metadata(cas_storage_t *s, const char *path) {
    if (!s || !path) return -1;
    MDB_txn *txn;
    if (mdb_txn_begin(s->env, NULL, 0, &txn) != 0) return -1;
    MDB_val key = { .mv_size = strlen(path) + 1, .mv_data = (void*)path };
    int rc = mdb_del(txn, s->dbi_chunks, &key, NULL);
    if (rc != 0) { mdb_txn_abort(txn); return -1; }
    return mdb_txn_commit(txn);
}

int cas_storage_list_dir(cas_storage_t *s, const char *path, void *buf, void *filler) {
    if (!s || !path || !buf || !filler) return -1;

    MDB_txn *txn;
    if (mdb_txn_begin(s->env, NULL, MDB_RDONLY, &txn) != 0) return -1;

    MDB_cursor *cursor;
    if (mdb_cursor_open(txn, s->dbi_chunks, &cursor) != 0) {
        mdb_txn_abort(txn);
        return -1;
    }

    size_t path_len = strlen(path);
    MDB_val key, mdbval;

    while (mdb_cursor_get(cursor, &key, &mdbval, MDB_NEXT) == 0) {
        // Skip binary hash keys (exactly 32 bytes)
        if (key.mv_size == BLAKE3_HASH_LEN) continue;
        
        // Keys are null-terminated strings for metadata and chunk entries
        if (key.mv_size == 0 || ((char*)key.mv_data)[key.mv_size - 1] != '\0') continue;
        const char *k = key.mv_data;

        // Skip chunk-index keys
        if (strncmp(k, "chunk:", 6) == 0) continue;

        // Is this entry a direct child of 'path'?
        if (path_len == 1 && path[0] == '/') {
            // root: child is /name (exactly one slash after root)
            const char *p = k + 1;
            if (*p == '\0') continue; // the root itself
            if (strchr(p, '/') != NULL) continue; // nested
        } else {
            if (strncmp(k, path, path_len) != 0) continue;
            if (k[path_len] != '/') continue;
            const char *p = k + path_len + 1;
            if (*p == '\0') continue;
            if (strchr(p, '/') != NULL) continue;
        }

        const char *name = (path_len == 1 && path[0] == '/') ? k + 1 : k + path_len + 1;
        if (*name == '\0') continue;

        fuse_fill_dir_t fill = (fuse_fill_dir_t)filler;
        fill(buf, name, NULL, 0, 0);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return 0;
}

static void make_chunk_key(char *out, const char *path, size_t chunk_idx) {
    snprintf(out, PATH_MAX_LEN + 32, "chunk:%s:%zu", path, chunk_idx);
}

int cas_storage_get_file_chunk_hash(cas_storage_t *s, const char *path, size_t chunk_idx, uint8_t *hash) {
    if (!s || !path || !hash) return -1;
    MDB_txn *txn;
    if (mdb_txn_begin(s->env, NULL, MDB_RDONLY, &txn) != 0) return -1;
    char kbuf[PATH_MAX_LEN + 32];
    make_chunk_key(kbuf, path, chunk_idx);
    MDB_val key = { .mv_size = strlen(kbuf) + 1, .mv_data = kbuf };
    MDB_val mdbval;
    int rc = mdb_get(txn, s->dbi_chunks, &key, &mdbval);
    if (rc != 0) { mdb_txn_abort(txn); return -1; }
    if (mdbval.mv_size != BLAKE3_HASH_LEN) { mdb_txn_abort(txn); return -1; }
    memcpy(hash, mdbval.mv_data, BLAKE3_HASH_LEN);
    mdb_txn_abort(txn);
    return 0;
}

int cas_storage_set_file_chunk_hash(cas_storage_t *s, const char *path, size_t chunk_idx, const uint8_t *hash) {
    if (!s || !path || !hash) return -1;
    MDB_txn *txn;
    if (mdb_txn_begin(s->env, NULL, 0, &txn) != 0) return -1;
    char kbuf[PATH_MAX_LEN + 32];
    make_chunk_key(kbuf, path, chunk_idx);
    MDB_val key = { .mv_size = strlen(kbuf) + 1, .mv_data = kbuf };
    MDB_val mdbval = { .mv_size = BLAKE3_HASH_LEN, .mv_data = (void*)hash };
    int rc = mdb_put(txn, s->dbi_chunks, &key, &mdbval, 0);
    if (rc != 0) { mdb_txn_abort(txn); return -1; }
    return mdb_txn_commit(txn);
}

extern int cas_storage_put(cas_storage_t *storage, const cas_hash_t *hash, const void *data, uint32_t len);
extern int cas_storage_get(cas_storage_t *storage, const cas_hash_t *hash, void *output, uint32_t *out_len);

int cas_storage_put_chunk(cas_storage_t *s, const uint8_t *hash, const uint8_t *data, uint32_t len) {
    if (!s || !hash || !data) return -1;
    cas_hash_t h;
    memcpy(h.bytes, hash, BLAKE3_HASH_LEN);
    return cas_storage_put(s, &h, data, len);
}

int cas_storage_get_chunk(cas_storage_t *s, const uint8_t *hash, uint8_t *data, uint32_t *len) {
    if (!s || !hash || !data || !len) return -1;
    cas_hash_t h;
    memcpy(h.bytes, hash, BLAKE3_HASH_LEN);
    uint32_t out_len = *len;
    int rc = cas_storage_get(s, &h, data, &out_len);
    if (rc == 0) *len = out_len;
    return rc;
}
