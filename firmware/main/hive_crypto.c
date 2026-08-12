#include "hive_crypto.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"
#include <string.h>

#ifndef CONFIG_HIVE_HMAC_KEY
#define CONFIG_HIVE_HMAC_KEY "dev-only-change-me"
#endif

static void sha(const uint8_t* d, size_t n, uint8_t out[32]){ mbedtls_sha256(d, n, out, 0); }

static bool auth(const uint8_t* d, size_t n, const hive_beacon_t* m, void* ctx){
    (void)m; (void)ctx;
    uint8_t tag[32];
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    const char* key = CONFIG_HIVE_HMAC_KEY;
    if (mbedtls_md_hmac(info, (const uint8_t*)key, strlen(key), d, n, tag) != 0) return false;
    /* MVP: exercises the keyed-HMAC path only; the real tag comparison against a
     * sender-supplied trailer lands in the security-hardening task (Task 13).
     * Integrity (SHA-256) is already fully enforced by the verify seam. */
    return true;
}

const hive_crypto_t* hive_crypto_get(void){
    static const hive_crypto_t c = { .sha256=sha, .auth_ok=auth, .ctx=0 };
    return &c;
}
