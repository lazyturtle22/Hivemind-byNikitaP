#pragma once
#include <stdint.h>
#include <stdbool.h>

#define HIVE_MAX_CHUNKS 8192

typedef struct { uint16_t count; uint8_t bits[HIVE_MAX_CHUNKS/8]; } chunker_t;

uint16_t hive_chunk_count(uint32_t image_size, uint16_t chunk_size);
void chunker_init(chunker_t*, uint16_t count);
void chunker_mark(chunker_t*, uint16_t idx);
bool chunker_has(const chunker_t*, uint16_t idx);
bool chunker_complete(const chunker_t*);
int  chunker_next_missing(const chunker_t*, uint16_t from);
