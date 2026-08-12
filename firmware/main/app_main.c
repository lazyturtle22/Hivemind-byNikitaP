#include <stdio.h>
#include "esp_log.h"
#include "hive_chip.h"

static const char *TAG = "hive";

void app_main(void) {
    ESP_LOGI(TAG, "HIVE boot v%lu", (unsigned long)HIVE_APP_VERSION);
}
