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

/* test_trickle.c */
void test_trickle_starts_at_imin(void);
void test_trickle_suppresses_when_k_consistent(void);
void test_trickle_doubles_and_caps(void);
void test_trickle_inconsistent_resets_to_imin(void);

/* test_chunker.c */
void test_chunk_count_rounds_up(void);
void test_chunker_marks_and_completes(void);

/* test_verify.c */
void test_verify_accepts_good(void);
void test_verify_rejects_bad_magic(void);
void test_verify_rejects_wrong_chip(void);
void test_verify_rejects_not_newer(void);
void test_verify_rejects_bad_hash(void);
void test_verify_rejects_bad_auth(void);

void app_main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_accepts_strictly_newer);
    RUN_TEST(test_rejects_same_version);
    RUN_TEST(test_rejects_older_version);
    RUN_TEST(test_accepts_big_jump);
    RUN_TEST(test_beacon_roundtrip);
    RUN_TEST(test_beacon_rejects_bad_magic);
    RUN_TEST(test_beacon_rejects_short);
    RUN_TEST(test_trickle_starts_at_imin);
    RUN_TEST(test_trickle_suppresses_when_k_consistent);
    RUN_TEST(test_trickle_doubles_and_caps);
    RUN_TEST(test_trickle_inconsistent_resets_to_imin);
    RUN_TEST(test_chunk_count_rounds_up);
    RUN_TEST(test_chunker_marks_and_completes);
    RUN_TEST(test_verify_accepts_good);
    RUN_TEST(test_verify_rejects_bad_magic);
    RUN_TEST(test_verify_rejects_wrong_chip);
    RUN_TEST(test_verify_rejects_not_newer);
    RUN_TEST(test_verify_rejects_bad_hash);
    RUN_TEST(test_verify_rejects_bad_auth);
    int failures = UNITY_END();
    if (failures != 0) {
        exit(failures);  /* propagate red tests as a non-zero exit code */
    }
}
