#include "unity.h"
#include <string.h>
#include "hive_beacon.h"

void test_beacon_roundtrip(void) {
    hive_beacon_t b = { .magic=HIVE_MAGIC, .type=HIVE_MSG_BEACON, .chip_id=1,
                        .version=7, .chunk_count=42, .chunk_size=200 };
    for (int i=0;i<32;i++) b.sha256[i]=(uint8_t)i;
    uint8_t buf[250];
    size_t n = hive_beacon_pack(&b, buf, sizeof buf);
    TEST_ASSERT_TRUE(n > 0 && n <= 250);
    hive_beacon_t out;
    TEST_ASSERT_TRUE(hive_beacon_unpack(buf, n, &out));
    TEST_ASSERT_EQUAL_UINT32(7, out.version);
    TEST_ASSERT_EQUAL_UINT16(42, out.chunk_count);
    TEST_ASSERT_EQUAL_UINT16(200, out.chunk_size);
    TEST_ASSERT_EQUAL_UINT8(1, out.chip_id);
    TEST_ASSERT_EQUAL_MEMORY(b.sha256, out.sha256, 32);
}
void test_beacon_rejects_bad_magic(void) {
    uint8_t buf[64]={0}; hive_beacon_t out;
    TEST_ASSERT_FALSE(hive_beacon_unpack(buf, sizeof buf, &out));
}
void test_beacon_rejects_short(void) {
    uint8_t buf[4]={0}; hive_beacon_t out;
    TEST_ASSERT_FALSE(hive_beacon_unpack(buf, 3, &out));
}
