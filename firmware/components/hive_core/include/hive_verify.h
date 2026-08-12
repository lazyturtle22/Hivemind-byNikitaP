#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "hive_beacon.h"

typedef enum { HIVE_OK=0, HIVE_ERR_MAGIC, HIVE_ERR_CHIP, HIVE_ERR_VERSION,
               HIVE_ERR_INTEGRITY, HIVE_ERR_AUTH } hive_verdict_t;

typedef struct {
    void (*sha256)(const uint8_t* data, size_t len, uint8_t out[32]);
    bool (*auth_ok)(const uint8_t* data, size_t len, const hive_beacon_t* meta, void* ctx);
    void* ctx;
} hive_crypto_t;

hive_verdict_t hive_verify_image(const hive_beacon_t* meta, uint32_t current_version,
                                 const uint8_t* image, size_t image_len,
                                 const hive_crypto_t* crypto);
