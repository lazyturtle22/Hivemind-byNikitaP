#include "hive_verify.h"
#include "hive_version.h"
#include "hive_chip.h"
#include <string.h>

hive_verdict_t hive_verify_image(const hive_beacon_t* m, uint32_t cur,
                                 const uint8_t* img, size_t len, const hive_crypto_t* c){
    if (m->magic != HIVE_MAGIC) return HIVE_ERR_MAGIC;
    if (!hive_chip_matches(m->chip_id)) return HIVE_ERR_CHIP;
    if (!hive_version_should_accept(cur, m->version)) return HIVE_ERR_VERSION;
    uint8_t digest[32]; c->sha256(img, len, digest);
    if (memcmp(digest, m->sha256, 32) != 0) return HIVE_ERR_INTEGRITY;
    if (!c->auth_ok(img, len, m, c->ctx)) return HIVE_ERR_AUTH;
    return HIVE_OK;
}
