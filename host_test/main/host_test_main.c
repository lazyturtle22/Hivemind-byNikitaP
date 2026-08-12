#include <stdlib.h>
#include "unity.h"

/* test_version.c */
void test_accepts_strictly_newer(void);
void test_rejects_same_version(void);
void test_rejects_older_version(void);
void test_accepts_big_jump(void);

/* test_beacon.c */
void test_beacon_roundtrip(void);
void test_beacon_rejects_bad_magic(void);
void test_beacon_rejects_short(void);

void app_main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_accepts_strictly_newer);
    RUN_TEST(test_rejects_same_version);
    RUN_TEST(test_rejects_older_version);
    RUN_TEST(test_accepts_big_jump);
    RUN_TEST(test_beacon_roundtrip);
    RUN_TEST(test_beacon_rejects_bad_magic);
    RUN_TEST(test_beacon_rejects_short);
    int failures = UNITY_END();
    if (failures != 0) {
        exit(failures);  /* propagate red tests as a non-zero exit code */
    }
}
