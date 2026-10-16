/* SPDX-License-Identifier: MIT */
#include "nt98513_annexb.h"
static void clear(void *p,size_t n) { uint8_t *b=p;while(n--) *b++=0; }
static int codec_ok(enum nt98513_ab_codec c) {return c==NT98513_AB_H264 || c==NT98513_AB_H265;}
int nt98513_ab_init(struct nt98513_ab_iter *it,const uint8_t *data,size_t size)
{
 if(!it) return NT98513_AB_ARGUMENT;
 clear(it,sizeof(*it));it->terminal=NT98513_AB_ARGUMENT;
 if(size>NT98513_AB_MAX_BYTES) return it->terminal=NT98513_AB_LIMIT;
 if(!data && size) return it->terminal;
 it->data=data;it->size=size;it->terminal=0;return 0;
}
/* size has already been capped; zeros never exceeds size. */
static int prefix(const uint8_t *p,size_t size,size_t from,size_t *one,size_t *zeros)
{
 size_t i,z=0;
 for(i=from;i<size;++i) {
  if(p[i]==0) ++z;
  else {if(p[i]==1 && z>=2) {*one=i;*zeros=z;return 1;}z=0;}
 }
 return 0;
}
int nt98513_ab_next(struct nt98513_ab_iter *it,struct nt98513_ab_nal *out)
{
 size_t one,z,begin,end,next_one,next_z,i;
 if(!it || !out) return NT98513_AB_ARGUMENT;
 clear(out,sizeof(*out));if(it->terminal) return it->terminal;
 if(it->next==it->size) return it->terminal=NT98513_AB_END;
 if(!prefix(it->data,it->size,it->next,&one,&z)) return it->terminal=NT98513_AB_FRAMING;
 if(!it->started) for(i=0;i<one-z;++i) if(it->data[i]!=0) return it->terminal=NT98513_AB_FRAMING;
 begin=one+1;end=it->size;
 if(prefix(it->data,it->size,begin,&next_one,&next_z)) {
  end=next_one-next_z;it->next=next_one-(next_z>=3?3:2);
 } else {
  while(end>begin && it->data[end-1]==0) --end;
  it->next=it->size;
 }
 it->started=1;
 if(end==begin) return it->terminal=NT98513_AB_EMPTY;
 out->data=it->data+begin;out->offset=begin;out->length=end-begin;out->start_code_size=z>=3?4:3;
 return 0;
}
int nt98513_ab_classify(enum nt98513_ab_codec codec,const struct nt98513_ab_nal *n,struct nt98513_ab_header *h)
{
 unsigned t,ref;
 if(!h || !n || !codec_ok(codec) || (!n->data && n->length)) return NT98513_AB_ARGUMENT;
 clear(h,sizeof(*h));
 if(n->length<(codec==NT98513_AB_H264?1u:2u)) return NT98513_AB_TRUNCATED;
 if(n->data[0]&0x80) return NT98513_AB_HEADER;
 if(codec==NT98513_AB_H264) {
  t=n->data[0]&31;ref=(n->data[0]>>5)&3;h->type=t;h->ref_idc=ref;
  if(t==0 || t>=24 || t==14 || t==20 || t==21) return NT98513_AB_UNSUPPORTED;
  if((!ref && (t==5 || t==7 || t==8 || t==13 || t==15)) || (ref && (t==6 || (t>=9 && t<=12)))) return NT98513_AB_HEADER;
  if(t==7) h->kind=NT98513_AB_SPS;
  if(t==8) h->kind=NT98513_AB_PPS;
  if(t==5) h->kind=NT98513_AB_IDR;
 } else {
  t=(n->data[0]>>1)&63;h->type=t;h->layer_id=((n->data[0]&1)<<5)|(n->data[1]>>3);h->temporal_id_plus1=n->data[1]&7;
  if(!h->temporal_id_plus1) return NT98513_AB_HEADER;
  if(h->layer_id==63) return NT98513_AB_UNSUPPORTED;
  if(((t>=16 && t<=23) || t==32 || t==33 || t==36 || t==37) && h->temporal_id_plus1!=1) return NT98513_AB_HEADER;
  if((t==2 || t==3 || ((t==4 || t==5) && h->layer_id==0)) && h->temporal_id_plus1==1) return NT98513_AB_HEADER;
  if(t==37 && h->layer_id!=0) return NT98513_AB_HEADER;
  if(t==32) h->kind=NT98513_AB_VPS;
  else if(t==33) h->kind=NT98513_AB_SPS;
  else if(t==34) h->kind=NT98513_AB_PPS;
  else if(t==19 || t==20) h->kind=NT98513_AB_IDR;
  else if(t>=16 && t<=18) h->kind=NT98513_AB_BLA;
  else if(t==21) h->kind=NT98513_AB_CRA;
  else if(t==22 || t==23) h->kind=NT98513_AB_OTHER_IRAP;
 }
 return 0;
}
int nt98513_ab_collect(enum nt98513_ab_codec codec,const uint8_t *data,size_t size,size_t max_nals,struct nt98513_ab_config *c)
{
 struct nt98513_ab_iter it;struct nt98513_ab_nal n;struct nt98513_ab_header h;int rc;
 if(!c) return NT98513_AB_ARGUMENT;
 clear(c,sizeof(*c));c->missing=codec==NT98513_AB_H265?7:3;
 if(!codec_ok(codec) || !max_nals || max_nals>NT98513_AB_MAX_NALS) return NT98513_AB_ARGUMENT;
 rc=nt98513_ab_init(&it,data,size);if(rc) return rc;
 while((rc=nt98513_ab_next(&it,&n))==0) {
  size_t *count=0;struct nt98513_ab_nal *first=0;unsigned bit=0;
  if(c->nal_count==max_nals) return NT98513_AB_LIMIT;
  rc=nt98513_ab_classify(codec,&n,&h);if(rc) return rc;
  ++c->nal_count;
  switch(h.kind) {
   case NT98513_AB_SPS:count=&c->sps_count;first=&c->sps;bit=1;break;
   case NT98513_AB_PPS:count=&c->pps_count;first=&c->pps;bit=2;break;
   case NT98513_AB_VPS:count=&c->vps_count;first=&c->vps;bit=4;break;
   case NT98513_AB_IDR:++c->idr_count;break;
   case NT98513_AB_BLA:++c->bla_count;break;
   case NT98513_AB_CRA:++c->cra_count;break;
   case NT98513_AB_OTHER_IRAP:++c->other_irap_count;break;
   default:break;
  }
  if(count) {if(*count) c->duplicates|=bit;else *first=n;++*count;c->missing&=~bit;}
 }
 if(rc!=NT98513_AB_END) return rc;
 c->complete=1;return 0;
}
