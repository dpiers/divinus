/* SPDX-License-Identifier: MIT */
#ifndef NT98513_DIVINUS_BRIDGE_H
#define NT98513_DIVINUS_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#include <hal/types.h> /* Actual pinned Divinus header; no replacement structs. */
#include "nt98513_annexb.h"

#define NT98513_DIVINUS_MAX_NALS 8u
#define NT98513_DIVINUS_MAX_TIMESTAMP_US (UINT64_MAX / UINT64_C(9))
enum nt98513_divinus_status {
    NT98513_DIVINUS_OK = 0,
    NT98513_DIVINUS_ARGUMENT = -1,
    NT98513_DIVINUS_SIZE = -2,
    NT98513_DIVINUS_TIMESTAMP = -3,
    NT98513_DIVINUS_PARSE = -4,
    NT98513_DIVINUS_TOO_MANY_NALS = -5
};
/* Counts describe fixed headers in this input only. Repetition is by type,
 * not parameter-set ID or byte equality; missing sets are permitted. */
struct nt98513_divinus_observations {
    unsigned nal_count, sps_count, pps_count, vps_count;
    unsigned idr_count, bla_count, cra_count, other_irap_count;
    unsigned parameter_sets, repeated_parameter_sets;
};
struct nt98513_divinus_result {
    int validation_status;
    int parser_status; /* Exact parser failure only when validation is PARSE. */
    unsigned callback_called;
    int callback_status; /* Meaningful only when callback_called is nonzero. */
    struct nt98513_divinus_observations observations; /* Valid only on OK. */
};
typedef int (*nt98513_divinus_callback)(void *context, hal_vidcodec codec,
    hal_vidstream *stream, const struct nt98513_divinus_observations *observations);

/* Submit a complete access unit supplied and owned by the CALLER, synchronously.
 * The adapter neither allocates nor copies frame bytes, acquires a frame,
 * establishes an access-unit boundary, nor validates RBSP/decodability.
 *
 * data[0..size) must remain readable and immutable for the entire call, including
 * the callback. The actual Divinus pointer is mutable: caller and callback must
 * enforce immutability. Callback must not retain stream/pack/observation pointers
 * or assume input lifetime after return; it may make its own complete copy.
 * Reentrant calls with independent arguments are permitted. No global state.
 *
 * codec must be explicit H264/H265. size must be 1..NT98513_AB_MAX_BYTES.
 * timestamp_us must be an explicitly supplied MICROSECOND value in
 * 1..UINT64_MAX/9: zero (Divinus wall-clock fallback) and multiplication overflow
 * are rejected. No conversion or inference from firmware timestamp fields.
 * Sequence is copied unchanged. All NALs are validated before ONE callback;
 * more than eight are rejected without truncation or partial delivery.
 *
 * pack.data/length preserve the entire original input including zero padding.
 * pack.offset=0; each NAL offset points to its original 3/4-byte start code and
 * length includes that prefix plus the parser's payload (not inter-NAL padding).
 * NAL type is the raw codec-specific type, without HEVC-to-H264 remapping.
 *
 * result must be writable and must not overlap input. Null result or detectable
 * input/result overlap for a size within the supported range returns ARGUMENT
 * without writing result. Oversize input is rejected without reading its bytes;
 * its alleged span is not used for overlap detection. Otherwise result
 * is cleared first; observations remain zero on validation failure. A successful
 * validation returns OK even if callback returns an error: consult callback_status.
 * Callback return is not proof of delivery. Callback must not modify result.
 * No stateful parameter replacement or downstream consumer correctness is implied.
 */
int nt98513_divinus_bridge_submit(enum nt98513_ab_codec codec,
    unsigned char *data, size_t size, uint32_t sequence, uint64_t timestamp_us,
    nt98513_divinus_callback callback, void *context,
    struct nt98513_divinus_result *result);
#endif
