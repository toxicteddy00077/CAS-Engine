#include <stddef.h>
#include <stdint.h>
#include <blake3.h>
#include <string.h>
#include <sodium.h>

#include "../include/types.h"
#include "../include/errors.h"


int cas_crypto_hash(const void *input, size_t len, cas_hash_t *output) {
    if(!input || !output) return ERROR_EXIT;

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, input, len);
    blake3_hasher_finalize(&hasher, output->bytes, BLAKE3_HASH_LEN);
    return 0;
}

int cas_hash_to_hex(const cas_hash_t *hash, char *hex) {
    if(!hex || !hash) return ERROR_EXIT;

    for (int i = 0; i < BLAKE3_HASH_LEN; i++) {
        sprintf(hex + i * 2, "%02x", hash->bytes[i]);
    }
    hex[BLAKE3_HASH_LEN * 2] = '\0';
    return 0;
}

int cas_hex_to_hash(const char *hex, cas_hash_t *hash) {
    if(!hex || !hash) return ERROR_EXIT;

    unsigned int val=0;
    for (int i = 0; i < BLAKE3_HASH_LEN; i++) {
        sscanf(hex + i * 2, "%02x", &val);
        hash->bytes[i] = (uint8_t) val;
    }
    return 0;
}

int cas_encrypt(const uint8_t *p_text, size_t len, const uint8_t key[32], void *c_text, size_t c_len) {
    if (!p_text || !key || !c_text) return -1;

    uint8_t nonce[NONCE_LEN];
    randombytes_buf(nonce, NONCE_LEN);

    memcpy(c_text, nonce, NONCE_LEN);

    if (crypto_secretbox_easy((uint8_t *) c_text + NONCE_LEN, p_text, len - NONCE_LEN, key) != 0) return ERROR_CRYPTO_FAILED;

    return 0;
}

int cas_decrypt(const uint8_t *c_text, size_t len, const uint8_t key[32], void *p_text, size_t p_len) {
    if (!p_text || !key || !c_text) return -1;

    if (len < NONCE_LEN) return ERROR_CRYPTO_FAILED;

    const uint8_t *nonce = c_text;
    if (crypto_secretbox_open_easy(p_text, nonce + NONCE_LEN, len - NONCE_LEN, key) != 0) return ERROR_CRYPTO_FAILED;

    return 0;
}
