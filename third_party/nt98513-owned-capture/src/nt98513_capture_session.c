/* SPDX-License-Identifier: GPL-2.0-only */
#include "nt98513_capture_session.h"
#include "../bsp/nt98513-owned-capture/userspace/reader_contract.h"
#include <errno.h>
#include <string.h>
struct delivery {
 const struct nt98513_session_sink *sink;
 void *context;
 const struct nt_oc_config *config;
 int changed;
 const struct nt98513_session_io *io;
 void *io_context;
 int cancellation;
};
static int deliver(void *p,hal_vidcodec c,hal_vidstream *s,
 const struct nt98513_divinus_observations *o)
{
 struct delivery *d=p;
 int rc=0;
 if(d->changed)rc=d->sink->reset(d->context,d->config->epoch,d->config->generation);
 if(rc)return rc;
 /* Neither callback is interrupted. A stop raised by reset prevents frame. */
 d->cancellation=d->io->cancelled(d->io_context);
 return d->cancellation?0:d->sink->frame(d->context,c,s,o);
}
int nt98513_capture_session_run(const struct nt98513_session_io *io,void *ctx,
 const struct nt98513_session_sink *sink,void *sinkctx,unsigned char *buf,
 size_t cap,struct nt98513_session_result *r)
{
 struct nt_oc_config cached={0},config;
 uint64_t previous_time=0,sequence=0;
 uint32_t record_generation=0;
 int fd,rc=0,err=0;
 if(!r)return NT98513_SESSION_ARGUMENT;
 memset(r,0,sizeof(*r));
 if(!io||!sink||!buf||cap<OC_CONFIG_BYTES+sizeof(struct nt_oc_frame_header)+1||
 !io->open||!io->wait||!io->cancelled||!io->read||!io->config||
 !io->arrival_us||!io->close||!sink->reset||!sink->frame)
  return r->status=NT98513_SESSION_ARGUMENT;
 fd=io->open(ctx);
 if(fd<0){r->io_error=fd;return r->status=NT98513_SESSION_IO;}
 for(;;){
  struct nt_oc_frame_header h;
  struct nt98513_divinus_result bridge;
  uint64_t timestamp=0;
  size_t readcap=cap-OC_CONFIG_BYTES;
  int events,changed;
  int64_t n;
  if(sequence>UINT32_MAX){rc=NT98513_SESSION_SEQUENCE;break;}
  err=io->cancelled(ctx);
  if(err){rc=err<0?NT98513_SESSION_IO:0;break;}
  events=io->wait(ctx,fd);
  if(events==-EINTR)continue;
  if(events<0){err=events;rc=NT98513_SESSION_IO;break;}
  if(events&NT98513_SESSION_STOP)break;
  err=io->cancelled(ctx);
  if(err){rc=err<0?NT98513_SESSION_IO:0;break;}
  /* ERR wins over DATA: do not accept a queued frame after transport failure. */
  if(events&NT98513_SESSION_ERROR){err=-EIO;rc=NT98513_SESSION_IO;break;}
  if(!(events&NT98513_SESSION_DATA))continue;
  if(readcap>NT98513_SESSION_MAX_AU+sizeof(h))readcap=NT98513_SESSION_MAX_AU+sizeof(h);
  n=io->read(ctx,fd,buf+OC_CONFIG_BYTES,readcap);
  if(n==-EAGAIN||n==-EINTR)continue;
  if(n<0){
   /* The real driver releases its reader slot on copy_to_user EFAULT.
    * EMSGSIZE instead preserves the slot; neither error is retried here. */
   if(n==-EFAULT){if(r->records_consumed!=UINT64_MAX)r->records_consumed++;}
   err=(int)n;rc=NT98513_SESSION_IO;break;
  }
  /* A positive successful read consumed one record, even if invalid below. */
  if(n>0){if(r->records_consumed!=UINT64_MAX)r->records_consumed++;}
  if(n<(int64_t)sizeof(h)||(uint64_t)n>readcap){rc=NT98513_SESSION_RECORD;break;}
  err=io->arrival_us(ctx,&timestamp);
  if(err){rc=NT98513_SESSION_IO;break;}
  if(!timestamp||timestamp>NT98513_DIVINUS_MAX_TIMESTAMP_US||timestamp<previous_time){rc=NT98513_SESSION_CLOCK;break;}
  previous_time=timestamp; /* Includes records subsequently discarded as stale. */
  memcpy(&h,buf+OC_CONFIG_BYTES,sizeof(h));
  r->last_token=h.token;r->last_dropped=h.dropped;
  if(h.magic!=NT_OC_FRAME_MAGIC||h.abi!=NT_OC_ABI||!h.bytes||!h.epoch||!h.generation||
     h.bytes>NT98513_SESSION_MAX_AU||h.flags!=NT_OC_FLAG_TIMESTAMP_UNAVAILABLE||
     (uint64_t)n!=sizeof(h)+(uint64_t)h.bytes){rc=NT98513_SESSION_RECORD;break;}
  err=io->cancelled(ctx);
  if(err){rc=err<0?NT98513_SESSION_IO:0;break;}
  memset(&config,0,sizeof(config));err=io->config(ctx,fd,&config);
  if(err){rc=NT98513_SESSION_IO;break;}
  /* Check each identity before admitting the narrow newer-config race. */
  if(config.abi!=NT_OC_ABI||!config.epoch||!config.generation||
     !config.bytes||config.bytes>OC_CONFIG_BYTES){rc=NT98513_SESSION_CONFIG;break;}
  if(h.epoch!=config.epoch||(cached.epoch&&cached.epoch!=config.epoch)){
   rc=NT98513_SESSION_EPOCH;break;
  }
  if(h.generation<record_generation||config.generation<h.generation||
    (cached.epoch&&(config.generation<cached.generation||
     (config.generation==cached.generation&&(config.bytes!=cached.bytes||
      memcmp(config.data,cached.data,config.bytes)))))){
   rc=NT98513_SESSION_CONFIG;break;
  }
  record_generation=h.generation;
  /* cached is the latest observed identity, including not-yet-delivered config.
   * Never forget it across repeated stale reads or replace equal-generation bytes. */
  cached=config;
  if(config.generation>h.generation){
   if(r->stale_records_discarded!=UINT64_MAX)r->stale_records_discarded++;
   continue;
  }
  if(!nt_oc_config_matches(&h,&config)){rc=NT98513_SESSION_CONFIG;break;}
  changed=!r->epoch||r->generation!=config.generation;
  memmove(buf+config.bytes,buf+OC_CONFIG_BYTES+sizeof(h),h.bytes);
  memcpy(buf,config.data,config.bytes);
  err=io->cancelled(ctx);
  if(err){rc=err<0?NT98513_SESSION_IO:0;break;}
  struct delivery d={sink,sinkctx,&config,changed,io,ctx,0};
  rc=nt98513_divinus_bridge_submit(NT98513_AB_H264,buf,config.bytes+h.bytes,
       (uint32_t)sequence,timestamp,deliver,&d,&bridge);
  r->bridge_status=rc;r->sink_status=bridge.callback_status;
  if(rc){rc=NT98513_SESSION_BRIDGE;break;}
  if(bridge.callback_status){rc=NT98513_SESSION_SINK;break;}
  if(d.cancellation){err=d.cancellation;rc=err<0?NT98513_SESSION_IO:0;break;}
  sequence++;
  r->frames_delivered++;r->epoch=config.epoch;r->generation=config.generation;
 }
 if(rc==NT98513_SESSION_IO)r->io_error=err;
 r->close_error=io->close(ctx,fd);
 if(!rc&&r->close_error){rc=NT98513_SESSION_IO;r->io_error=r->close_error;}
 r->status=rc;return rc;
}
