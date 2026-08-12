#include "hive_beacon.h"
#include <string.h>

static void wr32(uint8_t*p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static void wr16(uint8_t*p,uint16_t v){p[0]=v;p[1]=v>>8;}
static uint32_t rd32(const uint8_t*p){return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t rd16(const uint8_t*p){return (uint16_t)(p[0]|p[1]<<8);}

size_t hive_beacon_pack(const hive_beacon_t* b, uint8_t* out, size_t cap){
    if (cap < HIVE_BEACON_WIRE_LEN) return 0;
    uint8_t*p=out; wr32(p,b->magic);p+=4; *p++=b->type; *p++=b->chip_id;
    wr32(p,b->version);p+=4; wr16(p,b->chunk_count);p+=2; wr16(p,b->chunk_size);p+=2;
    memcpy(p,b->sha256,32);p+=32;
    return (size_t)(p-out);
}

bool hive_beacon_unpack(const uint8_t* in, size_t len, hive_beacon_t* out){
    if (len < HIVE_BEACON_WIRE_LEN) return false;
    if (rd32(in) != HIVE_MAGIC) return false;
    const uint8_t*p=in; out->magic=rd32(p);p+=4; out->type=*p++; out->chip_id=*p++;
    out->version=rd32(p);p+=4; out->chunk_count=rd16(p);p+=2; out->chunk_size=rd16(p);p+=2;
    memcpy(out->sha256,p,32);
    return true;
}
