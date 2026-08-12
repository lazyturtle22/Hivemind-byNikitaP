idf_component_register(SRCS "app_main.c" "hive_crypto.c" INCLUDE_DIRS "." REQUIRES hive_core nvs_flash esp_wifi app_update esp_timer mbedtls)
