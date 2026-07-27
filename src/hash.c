#include <stddef.h>
#include <stdint.h>
#include <blake3.h>
#include <string.h>
#include <sodium.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "../include/types.h"

static uint8_t crypto_key[32] = {0};
static int crypto_initialized = 0;

static int load_or_create_key(const char *storage_dir) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/key.bin", storage_dir);

    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, crypto_key, sizeof(crypto_key));
        close(fd);
        if (n == (ssize_t)sizeof(crypto_key)) return 0;
    }

    if (sodium_init() < 0) return -1;
    randombytes_buf(crypto_key, sizeof(crypto_key));

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    ssize_t n = write(fd, crypto_key, sizeof(crypto_key));
    close(fd);
    return (n == (ssize_t)sizeof(crypto_key)) ? 0 : -1;
}

int cas_crypto_init(const char *storage_dir) {
    if (!storage_dir) return -1;
    if (load_or_create_key(storage_dir) != 0) return -1;
    crypto_initialized = 1;
    return 0;
}

static void ensure_crypto(void) {
    if (!crypto_initialized) {
        if (sodium_init() < 0) return;
        randombytes_buf(crypto_key, sizeof(crypto_key));
        crypto_initialized = 1;
    }
}

int cas_crypto_hash(const void *input, size_t len, uint8_t *output) {
    if(!input || !output) return -1;

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, input, len);
    blake3_hasher_finalize(&hasher, output, BLAKE3_HASH_LEN);
    return 0;
}

int cas_hash_to_hex(const cas_hash_t *hash, char *hex) {
    if(!hex || !hash) return -1;

    for (int i = 0; i < BLAKE3_HASH_LEN; i++) {
        sprintf(hex + i * 2, "%02x", hash->bytes[i]);
    }
    hex[BLAKE3_HASH_LEN * 2] = '\0';
    return 0;
}

int cas_hex_to_hash(const char *hex, cas_hash_t *hash) {
    if(!hex || !hash) return -1;

    unsigned int val=0;
    for (int i = 0; i < BLAKE3_HASH_LEN; i++) {
        sscanf(hex + i * 2, "%02x", &val);
        hash->bytes[i] = (uint8_t) val;
    }
    return 0;
}

int cas_crypto_encrypt(const uint8_t *p_text, size_t len, uint8_t *c_text, uint32_t *c_len) {
    if (!p_text || !c_text || !c_len) return -1;

    ensure_crypto();
    if (!crypto_initialized) return -1;

    uint8_t nonce[NONCE_LEN];
    randombytes_buf(nonce, NONCE_LEN);

    memcpy(c_text, nonce, NONCE_LEN);

    if (crypto_secretbox_easy(c_text + NONCE_LEN, p_text, len, nonce, crypto_key) != 0) return -1;
    *c_len = NONCE_LEN + len + crypto_secretbox_MACBYTES;
    return 0;
}

int cas_crypto_decrypt(const uint8_t *c_text, uint32_t len, uint8_t *p_text, uint32_t *p_len) {
    if (!p_text || !c_text || !p_len) return -1;

    ensure_crypto();
    if (!crypto_initialized) return -1;

    if (len < NONCE_LEN + crypto_secretbox_MACBYTES) return -1;

    const uint8_t *nonce = c_text;
    if (crypto_secretbox_open_easy(p_text, c_text + NONCE_LEN, len - NONCE_LEN, nonce, crypto_key) != 0) return -1;
    *p_len = len - NONCE_LEN - crypto_secretbox_MACBYTES;
    return 0;
}
