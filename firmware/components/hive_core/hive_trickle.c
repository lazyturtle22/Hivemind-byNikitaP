#include "hive_trickle.h"

void trickle_init(trickle_t* t, const trickle_cfg_t* c){ t->cfg=*c; t->interval_ms=c->imin_ms; t->counter=0; }
void trickle_hear_consistent(trickle_t* t){ if (t->counter < 255) t->counter++; }
void trickle_hear_inconsistent(trickle_t* t){ t->interval_ms=t->cfg.imin_ms; t->counter=0; }
bool trickle_should_transmit(const trickle_t* t){ return t->counter < t->cfg.k; }
uint32_t trickle_next_interval(trickle_t* t){
    uint32_t n = t->interval_ms * 2u;
    if (n > t->cfg.imax_ms) n = t->cfg.imax_ms;
    t->interval_ms = n; t->counter = 0; return n;
}
