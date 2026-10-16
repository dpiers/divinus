/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Daniel Piers
 */
#include "owned_rtcp.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read32(const uint8_t *p)
{
    uint32_t n = 0;
    for (unsigned i = 0; i < 4; ++i) n = n * 256u + p[i];
    return n;
}
static size_t emit(uint8_t *out, size_t cap, const uint8_t *name, size_t n)
{
    return owned_rtcp_serialize_sr_sdes(out, cap, 0x12345678, 0xabcdef01,
        0x80000000, 0x89abcdef, 0xffffffff, 0x01020304, name, n);
}
/* Independent packet walk: consume wire lengths and item END, rather than
 * reconstructing the serializer's rounding expression. */
static void decode(const uint8_t *p, size_t n, const uint8_t *name, size_t len)
{
    size_t pos = 0;
    unsigned packets = 0;
    while (pos < n) {
        assert(n - pos >= 4);
        size_t bytes = ((size_t)p[pos+2] * 256 + p[pos+3] + 1) * 4;
        assert(bytes <= n - pos);
        if (packets == 0) {
            assert(p[pos] == 0x80 && p[pos+1] == 200 && bytes == 28);
            const uint32_t expected[] = {0x12345678,0xabcdef01,0x80000000,
                0x89abcdef,0xffffffff,0x01020304};
            for (unsigned i=0; i<6; ++i) assert(read32(p+pos+4+4*i)==expected[i]);
        } else {
            assert(packets == 1 && p[pos] == 0x81 && p[pos+1] == 202);
            assert(read32(p+pos+4)==0x12345678);
            assert(p[pos+8]==1 && p[pos+9]==len);
            assert(!memcmp(p+pos+10,name,len));
            size_t end=pos+10+len;
            assert(end < pos+bytes && p[end++]==0);
            while (end < pos+bytes) assert(p[end++]==0);
            assert(bytes-(10+len)>=1 && bytes-(10+len)<=4);
        }
        pos += bytes; ++packets;
    }
    assert(pos==n && packets==2);
}
int main(void)
{
    uint8_t name[255], storage[OWNED_RTCP_MAX_COMPOUND_SIZE+2];
    memset(name,'A',sizeof name);
    const uint8_t golden[] = {
        0x80,200,0,6,0x12,0x34,0x56,0x78,0xab,0xcd,0xef,1,
        0x80,0,0,0,0x89,0xab,0xcd,0xef,0xff,0xff,0xff,0xff,
        1,2,3,4,0x81,202,0,2,0x12,0x34,0x56,0x78,1,1,'A',0};
    assert(emit(storage,sizeof storage,name,1)==sizeof golden);
    assert(!memcmp(storage,golden,sizeof golden));
    for (size_t len=1; len<=255; ++len) {
        memset(storage,0xa5,sizeof storage);
        size_t n=emit(storage+1,sizeof storage-2,name,len);
        assert(n>=40 && n<=296 && n%4==0);
        assert(storage[0]==0xa5 && storage[n+1]==0xa5);
        decode(storage+1,n,name,len);
        /* Exact heap allocations expose both overreads and overwrites to ASan. */
        uint8_t *exact=malloc(n), *input=malloc(len);
        assert(exact && input); memcpy(input,name,len);
        assert(emit(exact,n,input,len)==n); decode(exact,n,name,len);
        for (size_t cap=0; cap<n; ++cap) {
            memset(exact,0xa5,n); assert(emit(exact,cap,input,len)==0);
            for(size_t j=0;j<n;++j) assert(exact[j]==0xa5);
        }
        free(input); free(exact);
    }
    memset(storage,0xa5,sizeof storage);
    assert(!emit(NULL,sizeof storage,name,1));
    assert(!emit(storage,sizeof storage,NULL,1));
    assert(!emit(storage,sizeof storage,name,0));
    assert(!emit(storage,sizeof storage,name,256));
    assert(!emit(storage,sizeof storage,name,SIZE_MAX));
    for(size_t i=0;i<sizeof storage;++i) assert(storage[i]==0xa5);
    /* Input overlaps both earlier and later parts of the destination. */
    for(size_t off=0;off<=40;++off) {
        memcpy(storage+off,name,255);
        assert(emit(storage,sizeof storage,storage+off,255)==296);
        decode(storage,296,name,255);
    }
    /* Values are serialized verbatim, including wrap-to-zero counts. */
    assert(owned_rtcp_serialize_sr_sdes(storage,sizeof storage,0,0,0,0,0,0,name,1)==40);
    for(size_t i=4;i<28;++i) assert(storage[i]==0);
    for(size_t i=32;i<36;++i) assert(storage[i]==0);
    puts("owned RTCP: golden bytes, 255 lengths, every short capacity, overlap and decoder checks passed");
    return 0;
}
