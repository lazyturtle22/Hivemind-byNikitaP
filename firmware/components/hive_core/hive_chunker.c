#include "hive_chunker.h"
#include <string.h>

uint16_t hive_chunk_count(uint32_t sz, uint16_t cs){ return cs ? (uint16_t)((sz + cs - 1)/cs) : 0; }
void chunker_init(chunker_t* c, uint16_t n){ c->count = n; memset(c->bits,0,sizeof c->bits); }
void chunker_mark(chunker_t* c, uint16_t i){ if (i < c->count) c->bits[i>>3] |= (uint8_t)(1u << (i&7)); }
bool chunker_has(const chunker_t* c, uint16_t i){ return i < c->count && ((c->bits[i>>3] >> (i&7)) & 1u); }
bool chunker_complete(const chunker_t* c){
    for (uint16_t i=0;i<c->count;i++) if (!chunker_has(c,i)) return false;
    return c->count > 0;
}
int chunker_next_missing(const chunker_t* c, uint16_t from){
    for (uint16_t i=from;i<c->count;i++) if (!chunker_has(c,i)) return i;
    return -1;
}
