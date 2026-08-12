#include "unity.h"
#include "hive_version.h"

void test_accepts_strictly_newer(void)   { TEST_ASSERT_TRUE(hive_version_should_accept(1, 2)); }
void test_rejects_same_version(void)      { TEST_ASSERT_FALSE(hive_version_should_accept(2, 2)); }
void test_rejects_older_version(void)     { TEST_ASSERT_FALSE(hive_version_should_accept(5, 3)); }
void test_accepts_big_jump(void)          { TEST_ASSERT_TRUE(hive_version_should_accept(1, 100)); }
