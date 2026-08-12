#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint32_t imin_ms, imax_ms; uint8_t k; } trickle_cfg_t;
typedef struct { trickle_cfg_t cfg; uint32_t interval_ms; uint8_t counter; } trickle_t;

void trickle_init(trickle_t*, const trickle_cfg_t*);
void trickle_hear_consistent(trickle_t*);
void trickle_hear_inconsistent(trickle_t*);
bool trickle_should_transmit(const trickle_t*);
uint32_t trickle_next_interval(trickle_t*);
