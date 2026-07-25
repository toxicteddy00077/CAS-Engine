#include "../include/types.h"
#include <lmdb.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>

cas_storage_t* cas_storage_init(const char* storage_dir);
void cas_storage_close(cas_storage_t* storage);

bool cas_duplication_check(cas_storage_t* storage, const cas_hash_t* hash);
int cas_storage_put_chunk(cas_storage_t* storage, const cas_hash_t* hash, const void* data, uint32_t len);
int cas_storage_get_chunk(cas_storage_t* storage, const cas_hash_t* hash, void* out_buffer, uint32_t* out_len);

//create or open seg file
static int open_seg_file(cas_storage_t *storage, uint32_t seg_id){
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/segment_%04u.blob", storage->store_dir, seg_id);

    int fd = fopen(filepath, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        return -1;
    }

    off_t offset_len = lseek(fd, 0, SEEK_END);
    if (offset_len == -1) {
        close(fd);
        return -1;
    }

    storage->active_fd = fd;
    storage->active_seg_id = seg_id;
    storage->active_offset = (uint64_t)offset_len;
}


//init
cas_storage_t* cas_store_init(const char *storage_dir){
    if (mkdir(storage_dir, 0755) != 0) return NULL;

    cas_storage_t* storage = calloc(1, sizeof(cas_storage_t));
    if (!storage) return NULL;

    storage->store_dir = strdup(storage_dir);
    storage->seg_size = MAX_SEGMENT_SIZE;

    // 1. Initialize LMDB Environment
    if (mdb_env_create(&storage->env) != 0) goto cleanup;

    // Set map size (e.g., 10 GB virtual memory map limit)
    if (mdb_env_set_mapsize(storage->env, (size_t)1 * 1024 * 1024 * 1024) != 0) goto cleanup;

    // Allow multiple named databases inside environment
    if (mdb_env_set_maxdbs(storage->env, 4) != 0) goto cleanup;

    // Open environment directory
    if (mdb_env_open(storage->env, storage->store_dir, MDB_NOSUBDIR, 0644) != 0) goto cleanup;

    // 2. Open "chunks" database table inside LMDB
    MDB_txn* txn;
    if (mdb_txn_begin(storage->env, NULL, 0, &txn) != 0) goto cleanup;
    if (mdb_dbi_open(txn, "chunks", MDB_CREATE, &storage->dbi_chunks) != 0) {
        mdb_txn_abort(txn);
        goto cleanup;
    }
    if (mdb_txn_commit(txn) != 0) goto cleanup;

    // 3. Open initial segment file (segment_0001.blob)
    if (open_seg_file(storage, 1) != 0) goto cleanup;

    return storage;

cleanup:
    cas_storage_close(storage);
    return NULL;
}

//close
void cas_storage_close(cas_storage_t* storage) {
    if (!storage) return;

    if (storage->active_fd >= 0) {
        close(storage->active_fd);
    }

    if (storage->env) {
        mdb_dbi_close(storage->env, storage->dbi_chunks);
        mdb_env_close(storage->env);
    }

    free(storage->store_dir);
    free(storage);
}

//++++++++++++++++++++++++++++++++ Actual storage API starts here++++++++++++++++++++++++++++++++


//duplicaiotn check
bool cas_duplication_check(cas_storage_t* storage, const cas_hash_t* hash) {
    if (!storage || !hash) return false;

    MDB_txn* txn;
    if (mdb_txn_begin(storage->env, NULL, MDB_RDONLY, &txn) != 0) return false;

    MDB_val key = { .mv_size = sizeof(hash->bytes),
                    .mv_data = (void*)hash->bytes };
    MDB_val data;

    int rc = mdb_get(txn, storage->dbi_chunks, &key, &data);
    mdb_txn_abort(txn); // Read transactions can just abort/close

    return (rc == 0); // 0 means key exists
}

//put
int cas_store_put(cas_storage_t *storage, cas_hash_t *hash, const void* data, uint32_t len){
    if (!storage || !hash) return false;

    if (cas_duplication_check(storage, hash)) { return 0; }

    if (storage->active_offset + len > storage->seg_size) {
        close(storage->active_fd);
        if (open_seg_file(storage, storage->active_seg_id + 1) != 0) {
            return -1;
        }
    }

    // Write raw encrypted buffer to current segment file offset
    ssize_t bytes_written = pwrite(storage->active_fd, data, len, storage->active_offset);
    if (bytes_written != (ssize_t)len) {
        return -1;
    }

    // Construct location record
    cas_loc_t loc = {
        .seg_id = storage->active_seg_id,
        .offset = storage->active_offset,
        .len = len
    };

    MDB_txn* txn;
    if (mdb_txn_begin(storage->env, NULL, 0, &txn) != 0) return false;

    MDB_val key = { .mv_size = sizeof(hash->bytes), .mv_data = (void*)hash->bytes };
    MDB_val val = { .mv_size = sizeof(cas_loc_t), .mv_data = (void*)&loc };

    if (mdb_put(txn, storage->dbi_chunks, &key, &val, 0) != 0) {
            mdb_txn_abort(txn);
            return -1;
        }

        if (mdb_txn_commit(txn) != 0) return -1;

        // Advance write head pointer
        storage->active_offset += len;
        return 0;
}

//get
int cas_store_get(cas_storage_t *storage, cas_hash_t *hash, const void* output, uint32_t out_len){
    if (!storage || !hash) return false;

    MDB_txn* txn;
    if (mdb_txn_begin(storage->env, NULL, 0, &txn) != 0) return false;

    MDB_val key = { .mv_size = sizeof(hash->bytes), .mv_data = (void*)hash->bytes };
    MDB_val val;
    if (mdb_get(txn, storage->dbi_chunks, &key, &val) != 0) {
        mdb_txn_abort(txn);
        return false;
    }
    cas_loc_t loc;
    memcpy(&loc, val.mv_data, sizeof(cas_loc_t));
    mdb_txn_abort(txn);

    // Open target segment file for reading
    char filepath[PATH_MAX_LEN];
    snprintf(filepath, sizeof(filepath), "%s/segment_%04u.blob", storage->store_dir, loc.seg_id);

    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    // Read directly from offset into user's output buffer
    fseek(fp, loc.offset, SEEK_SET);
    size_t bytes_read = fread(output, 1, loc.len, fp);
    fclose(fp);

    if (bytes_read != loc.len) {
        return -1;
    }

    out_len = loc.len;
    return 0;
}
