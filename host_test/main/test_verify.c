#include "unity.h"
#include <string.h>
#include "hive_verify.h"

/* mock: sha256 = first 32 bytes of image (so tests are deterministic) */
static void mock_sha(const uint8_t* d, size_t n, uint8_t out[32]){
    memset(out,0,32); for(size_t i=0;i<n&&i<32;i++) out[i]=d[i];
}
static bool g_auth = true;
static bool mock_auth(const uint8_t* d, size_t n, const hive_beacon_t* m, void* c){ (void)d;(void)n;(void)m;(void)c; return g_auth; }
static hive_crypto_t CRYPTO = { .sha256=mock_sha, .auth_ok=mock_auth, .ctx=0 };

static hive_beacon_t good_meta(const uint8_t* img, size_t n){
    hive_beacon_t m = { .magic=HIVE_MAGIC, .type=HIVE_MSG_BEACON, .chip_id=1, .version=2 };
    mock_sha(img, n, m.sha256); return m;
}
void test_verify_accepts_good(void){
    uint8_t img[40]; for(int i=0;i<40;i++) img[i]=(uint8_t)i;
    hive_beacon_t m = good_meta(img,sizeof img); g_auth=true;
    TEST_ASSERT_EQUAL_INT(HIVE_OK, hive_verify_image(&m, 1, img, sizeof img, &CRYPTO));
}
void test_verify_rejects_bad_magic(void){
    uint8_t img[40]={0}; hive_beacon_t m=good_meta(img,40); m.magic=0;
    TEST_ASSERT_EQUAL_INT(HIVE_ERR_MAGIC, hive_verify_image(&m,1,img,40,&CRYPTO));
}
void test_verify_rejects_wrong_chip(void){
    uint8_t img[40]={0}; hive_beacon_t m=good_meta(img,40); m.chip_id=9;
    TEST_ASSERT_EQUAL_INT(HIVE_ERR_CHIP, hive_verify_image(&m,1,img,40,&CRYPTO));
}
void test_verify_rejects_not_newer(void){
    uint8_t img[40]={0}; hive_beacon_t m=good_meta(img,40); /* version=2 */
    TEST_ASSERT_EQUAL_INT(HIVE_ERR_VERSION, hive_verify_image(&m,2,img,40,&CRYPTO));
}
void test_verify_rejects_bad_hash(void){
    uint8_t img[40]; for(int i=0;i<40;i++) img[i]=(uint8_t)i;
    hive_beacon_t m=good_meta(img,40); m.sha256[0]^=0xFF; /* corrupt expected */
    TEST_ASSERT_EQUAL_INT(HIVE_ERR_INTEGRITY, hive_verify_image(&m,1,img,40,&CRYPTO));
}
void test_verify_rejects_bad_auth(void){
    uint8_t img[40]; for(int i=0;i<40;i++) img[i]=(uint8_t)i;
    hive_beacon_t m=good_meta(img,40); g_auth=false;
    TEST_ASSERT_EQUAL_INT(HIVE_ERR_AUTH, hive_verify_image(&m,1,img,40,&CRYPTO));
    g_auth=true;
}
