#include "unity.h"
#include "hive_chunker.h"

void test_chunk_count_rounds_up(void){
    TEST_ASSERT_EQUAL_UINT16(5, hive_chunk_count(1000, 200));
    TEST_ASSERT_EQUAL_UINT16(6, hive_chunk_count(1001, 200));
    TEST_ASSERT_EQUAL_UINT16(1, hive_chunk_count(1, 200));
}
void test_chunker_marks_and_completes(void){
    chunker_t c; chunker_init(&c, 3);
    TEST_ASSERT_FALSE(chunker_complete(&c));
    TEST_ASSERT_EQUAL_INT(0, chunker_next_missing(&c, 0));
    chunker_mark(&c,0); chunker_mark(&c,2);
    TEST_ASSERT_TRUE(chunker_has(&c,0));
    TEST_ASSERT_FALSE(chunker_has(&c,1));
    TEST_ASSERT_EQUAL_INT(1, chunker_next_missing(&c, 0));
    chunker_mark(&c,1);
    TEST_ASSERT_TRUE(chunker_complete(&c));
    TEST_ASSERT_EQUAL_INT(-1, chunker_next_missing(&c, 0));
}
