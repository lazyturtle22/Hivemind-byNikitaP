#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define HIVE_MAGIC 0x48495645u   /* "HIVE" */

enum { HIVE_MSG_BEACON=1, HIVE_MSG_REQ=2, HIVE_MSG_DATA=3 };

typedef struct {
    uint32_t magic; uint8_t type; uint8_t chip_id;
    uint32_t version; uint16_t chunk_count; uint16_t chunk_size;
    uint8_t sha256[32];
} hive_beacon_t;

#define HIVE_BEACON_WIRE_LEN 46  /* 4+1+1+4+2+2+32 */

size_t hive_beacon_pack(const hive_beacon_t* b, uint8_t* out, size_t cap);
bool   hive_beacon_unpack(const uint8_t* in, size_t len, hive_beacon_t* out);
