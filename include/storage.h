#ifndef CAS_STORAGE_H
#define CAS_STORAGE_H

#include "types.h"

cas_storage_t* cas_storage_init(const char* storage_dir);
void cas_storage_close(cas_storage_t* storage);

int cas_storage_put_metadata(cas_storage_t *storage, const char *path, size_t size, bool is_dir);
int cas_storage_get_metadata(cas_storage_t *storage, const char *path, size_t *size, bool *is_dir);
int cas_storage_delete_metadata(cas_storage_t *storage, const char *path);

int cas_storage_list_dir(cas_storage_t *storage, const char *path, void *buf, void *filler);

int cas_storage_get_file_chunk_hash(cas_storage_t *storage, const char *path, size_t chunk_idx, uint8_t *hash);
int cas_storage_set_file_chunk_hash(cas_storage_t *storage, const char *path, size_t chunk_idx, const uint8_t *hash);

int cas_storage_put_chunk(cas_storage_t *storage, const uint8_t *hash, const uint8_t *data, uint32_t len);
int cas_storage_get_chunk(cas_storage_t *storage, const uint8_t *hash, uint8_t *data, uint32_t *len);

#endif
