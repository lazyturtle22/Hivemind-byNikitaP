#include "hive_version.h"

bool hive_version_should_accept(uint32_t current, uint32_t incoming) {
    return incoming > current;
}
