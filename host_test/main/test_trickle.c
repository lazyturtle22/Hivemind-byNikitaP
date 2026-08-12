#include "unity.h"
#include "hive_trickle.h"

static trickle_cfg_t CFG = { .imin_ms=100, .imax_ms=800, .k=2 };

void test_trickle_starts_at_imin(void){
    trickle_t t; trickle_init(&t,&CFG);
    TEST_ASSERT_EQUAL_UINT32(100, t.interval_ms);
    TEST_ASSERT_TRUE(trickle_should_transmit(&t)); /* counter 0 < k 2 */
}
void test_trickle_suppresses_when_k_consistent(void){
    trickle_t t; trickle_init(&t,&CFG);
    trickle_hear_consistent(&t); trickle_hear_consistent(&t); /* counter=2 == k */
    TEST_ASSERT_FALSE(trickle_should_transmit(&t));
}
void test_trickle_doubles_and_caps(void){
    trickle_t t; trickle_init(&t,&CFG);
    TEST_ASSERT_EQUAL_UINT32(200, trickle_next_interval(&t));
    TEST_ASSERT_EQUAL_UINT32(400, trickle_next_interval(&t));
    TEST_ASSERT_EQUAL_UINT32(800, trickle_next_interval(&t));
    TEST_ASSERT_EQUAL_UINT32(800, trickle_next_interval(&t)); /* capped */
}
void test_trickle_inconsistent_resets_to_imin(void){
    trickle_t t; trickle_init(&t,&CFG);
    trickle_next_interval(&t); trickle_next_interval(&t); /* now 400 */
    trickle_hear_inconsistent(&t);
    TEST_ASSERT_EQUAL_UINT32(100, t.interval_ms);
    TEST_ASSERT_TRUE(trickle_should_transmit(&t));
}
