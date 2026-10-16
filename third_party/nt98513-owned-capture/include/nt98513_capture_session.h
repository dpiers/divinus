/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NT98513_CAPTURE_SESSION_H
#define NT98513_CAPTURE_SESSION_H
#include "nt98513_divinus_bridge.h"
#include "../bsp/nt98513-owned-capture/linux/nt98513_capture_uapi.h"
#include "../bsp/nt98513-owned-capture/wire.h"
#define NT98513_SESSION_MAX_AU OC_MAX_FRAME
#define NT98513_SESSION_BUFFER_BYTES (OC_CONFIG_BYTES + sizeof(struct nt_oc_frame_header) + NT98513_SESSION_MAX_AU)
#define NT98513_SESSION_DATA 1
#define NT98513_SESSION_STOP 2
#define NT98513_SESSION_ERROR 4
enum nt98513_session_status {
 NT98513_SESSION_STOPPED=0, NT98513_SESSION_ARGUMENT=-1,
 NT98513_SESSION_IO=-2, NT98513_SESSION_RECORD=-3,
 NT98513_SESSION_CONFIG=-4, NT98513_SESSION_EPOCH=-5,
 NT98513_SESSION_CLOCK=-6, NT98513_SESSION_BRIDGE=-7,
 NT98513_SESSION_SINK=-8, NT98513_SESSION_SEQUENCE=-9
};
struct nt98513_session_io {
 int (*open)(void *);
 int (*wait)(void *,int); /* bitmask above, or negative errno; STOP wins */
 int (*cancelled)(void *); /* 0/1 or negative errno; must not block */
 int64_t (*read)(void *,int,void *,size_t); /* bytes or negative errno */
 int (*config)(void *,int,struct nt_oc_config *); /* 0 or negative errno */
 int (*arrival_us)(void *,uint64_t *); /* Linux CLOCK_MONOTONIC, not capture PTS */
 int (*close)(void *,int); /* once, never retried, 0 or negative errno */
};
struct nt98513_session_sink {
 int (*reset)(void *,uint32_t epoch,uint32_t generation);
 nt98513_divinus_callback frame;
};
struct nt98513_session_result {
 int status, io_error, close_error, bridge_status, sink_status;
 /* Positive reads plus driver EFAULT; delivered means sink returned zero,
  * not network delivery. Token/drop fields belong to the last valid-length header. */
 uint64_t records_consumed, frames_delivered, stale_records_discarded;
 uint32_t epoch,generation,last_token,last_dropped;
};
/* Single owner, synchronous, H264 only. No global state or allocation. All
 * arguments and callback tables remain valid and disjoint from buffer/result;
 * callbacks may not retain or mutate borrowed frame bytes. Each atomic read is
 * consumed once, never stitched/replayed. Exact configuration bytes prefix every
 * AU. The eight-NAL limit covers this combined record, with no deduplication.
 * First valid generation resets sink before delivery; subsequent generations
 * must increase within the same nonzero epoch. Epoch changes require a new run.
 * A valid same-epoch newer config discards the consumed stale AU and continues;
 * pending config identity is retained and checked across races. Record generation
 * cannot decrease; repeated equal stale records are allowed. Discards do not
 * advance sequence or reset the sink. Same-generation config changes fail.
 * Sink reset must clear downstream caches.
 * Cancellation is checked between operations and after reset before frame;
 * neither running callback can be interrupted.
 * Errors are terminal; a failing sink may already have emitted partial output.
 * Sequence starts at zero, independent of RTOS token; overflow ends the run.
 * Arrival timestamps are sampled after reads, nondecreasing including discarded
 * records (ties allowed). Consumed/discard counters saturate at UINT64_MAX.
 * Caller must separately run the watchdog keeper. Closing does not stop RTOS.
 */
int nt98513_capture_session_run(const struct nt98513_session_io *,void *,
 const struct nt98513_session_sink *,void *,unsigned char *,size_t,
 struct nt98513_session_result *);
/* Actual nonblocking Linux device adapter. cancel_fd must be a stable readable
 * stop pipe/event descriptor, distinct from device; readable/hangup stops. This
 * call owns only its device fd. No START/configuration/device activation. */
int nt98513_capture_session_posix(int cancel_fd,
 const struct nt98513_session_sink *,void *,unsigned char *,size_t,
 struct nt98513_session_result *);
#endif
