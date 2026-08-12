#include <stdlib.h>
#include "unity.h"

/* test_version.c */
void test_accepts_strictly_newer(void);
void test_rejects_same_version(void);
void test_rejects_older_version(void);
void test_accepts_big_jump(void);

void app_main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_accepts_strictly_newer);
    RUN_TEST(test_rejects_same_version);
    RUN_TEST(test_rejects_older_version);
    RUN_TEST(test_accepts_big_jump);
    int failures = UNITY_END();
    if (failures != 0) {
        exit(failures);  /* propagate red tests as a non-zero exit code */
    }
}
