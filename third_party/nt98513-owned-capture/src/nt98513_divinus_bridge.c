/* SPDX-License-Identifier: MIT */
#include "nt98513_divinus_bridge.h"
#include <limits.h>
_Static_assert(UINT_MAX == UINT32_MAX, "Divinus unsigned fields must be 32-bit");
_Static_assert(ULLONG_MAX >= UINT64_MAX, "Divinus timestamp must hold uint64_t");
_Static_assert(sizeof(((hal_vidpack *)0)->nalu) / sizeof(hal_vidnalu) == 8,
    "Pinned Divinus NAL capacity changed");
static void clear(void *p,size_t n) {
    volatile unsigned char *b=p; while(n--) *b++=0;
}
static int fail(struct nt98513_divinus_result *r,int status,int parser) {
    r->validation_status=status;r->parser_status=parser;return status;
}
static void observe(struct nt98513_divinus_observations *o,
    enum nt98513_ab_kind kind) {
    unsigned *count=0,bit=0;
    ++o->nal_count;
    switch(kind) {
        case NT98513_AB_SPS:count=&o->sps_count;bit=NT98513_AB_SPS_BIT;break;
        case NT98513_AB_PPS:count=&o->pps_count;bit=NT98513_AB_PPS_BIT;break;
        case NT98513_AB_VPS:count=&o->vps_count;bit=NT98513_AB_VPS_BIT;break;
        case NT98513_AB_IDR:++o->idr_count;break;
        case NT98513_AB_BLA:++o->bla_count;break;
        case NT98513_AB_CRA:++o->cra_count;break;
        case NT98513_AB_OTHER_IRAP:++o->other_irap_count;break;
        default:break;
    }
    if(count) {
        if(*count) o->repeated_parameter_sets|=bit;
        ++*count;o->parameter_sets|=bit;
    }
}
int nt98513_divinus_bridge_submit(enum nt98513_ab_codec codec,
    unsigned char *data, size_t size, uint32_t sequence, uint64_t timestamp_us,
    nt98513_divinus_callback callback, void *context,
    struct nt98513_divinus_result *result)
{
    hal_vidpack pack;hal_vidstream stream;
    struct nt98513_divinus_observations observations;
    struct nt98513_ab_iter iterator;struct nt98513_ab_nal nal;
    struct nt98513_ab_header header;int rc;
    uintptr_t first=(uintptr_t)data,out=(uintptr_t)result;
    if(!result) return NT98513_DIVINUS_ARGUMENT;
    /* Do not clear a result that would overwrite any caller-owned input byte.
     * Difference comparisons avoid forming an overflowing end address. */
    if(data && size && size<=NT98513_AB_MAX_BYTES && ((out>=first && out-first<size) ||
        (first>out && first-out<sizeof(*result)))) return NT98513_DIVINUS_ARGUMENT;
    clear(result,sizeof(*result));
    if(!data || !callback || (codec!=NT98513_AB_H264 && codec!=NT98513_AB_H265))
        return fail(result,NT98513_DIVINUS_ARGUMENT,0);
    if(!size || size>NT98513_AB_MAX_BYTES)
        return fail(result,NT98513_DIVINUS_SIZE,0);
    if(size-1>UINTPTR_MAX-first) return fail(result,NT98513_DIVINUS_ARGUMENT,0);
    if(!timestamp_us || timestamp_us>NT98513_DIVINUS_MAX_TIMESTAMP_US)
        return fail(result,NT98513_DIVINUS_TIMESTAMP,0);
    clear(&pack,sizeof(pack));clear(&stream,sizeof(stream));
    clear(&observations,sizeof(observations));
    rc=nt98513_ab_init(&iterator,data,size);
    if(rc) return fail(result,NT98513_DIVINUS_PARSE,rc);
    while((rc=nt98513_ab_next(&iterator,&nal))==NT98513_AB_OK) {
        size_t offset,length;
        if(observations.nal_count==NT98513_DIVINUS_MAX_NALS)
            return fail(result,NT98513_DIVINUS_TOO_MANY_NALS,0);
        rc=nt98513_ab_classify(codec,&nal,&header);
        if(rc) return fail(result,NT98513_DIVINUS_PARSE,rc);
        /* Parser offsets point at payload; HAL offsets point at its prefix. */
        if(nal.offset<nal.start_code_size || nal.offset>size || nal.length>size-nal.offset)
            return fail(result,NT98513_DIVINUS_PARSE,NT98513_AB_FRAMING);
        offset=nal.offset-nal.start_code_size;
        length=nal.start_code_size+nal.length;
        pack.nalu[observations.nal_count].offset=(unsigned)offset;
        pack.nalu[observations.nal_count].length=(unsigned)length;
        pack.nalu[observations.nal_count].type=(int)header.type;
        observe(&observations,header.kind);
    }
    if(rc!=NT98513_AB_END) return fail(result,NT98513_DIVINUS_PARSE,rc);
    pack.data=data;pack.length=(unsigned)size;pack.naluCnt=(int)observations.nal_count;
    pack.timestamp=timestamp_us;stream.pack=&pack;stream.count=1;stream.seq=sequence;
    result->observations=observations;result->callback_called=1;
    result->callback_status=callback(context,
        codec==NT98513_AB_H264?HAL_VIDCODEC_H264:HAL_VIDCODEC_H265,
        &stream,&observations);
    return NT98513_DIVINUS_OK;
}
