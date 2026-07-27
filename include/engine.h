#ifndef CAS_ENGINE_H
#define CAS_ENGINE_H

#include "types.h"

int cas_crypto_hash(const void *input, size_t len, uint8_t *output);
int cas_crypto_encrypt(const uint8_t *plain, size_t len, uint8_t *cipher, uint32_t *out_len);
int cas_crypto_decrypt(const uint8_t *cipher, uint32_t len, uint8_t *plain, uint32_t *out_len);

int cas_fuse_mount(const char *mountpoint, cas_storage_t *storage, int argc, char *argv[]);

#endif
