#pragma once
#include <stdint.h>
#include <stdbool.h>

#define HIVE_CHIP_C6 1

#ifndef HIVE_APP_VERSION
#define HIVE_APP_VERSION 1u   /* overridden at build time by flash.ps1 */
#endif

static inline bool hive_chip_matches(uint8_t chip_id) { return chip_id == HIVE_CHIP_C6; }
