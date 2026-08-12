#include <stdlib.h>
#include "unity.h"

void app_main(void) {
    UNITY_BEGIN();
    /* RUN_TEST lines added per module in later tasks */
    int failures = UNITY_END();
    if (failures != 0) {
        exit(failures);  /* propagate red tests as a non-zero exit code */
    }
}
