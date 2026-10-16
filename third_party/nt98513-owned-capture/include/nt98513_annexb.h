/* SPDX-License-Identifier: MIT */
#ifndef NT98513_ANNEXB_H
#define NT98513_ANNEXB_H
#include <stddef.h>
#include <stdint.h>
#define NT98513_AB_MAX_BYTES ((size_t)16777216)
#define NT98513_AB_MAX_NALS ((size_t)4096)
enum nt98513_ab_status { NT98513_AB_OK=0, NT98513_AB_END=1,
 NT98513_AB_ARGUMENT=-1, NT98513_AB_LIMIT=-2, NT98513_AB_FRAMING=-3,
 NT98513_AB_EMPTY=-4, NT98513_AB_TRUNCATED=-5, NT98513_AB_HEADER=-6,
 NT98513_AB_UNSUPPORTED=-7 };
enum nt98513_ab_codec { NT98513_AB_H264=1, NT98513_AB_H265=2 };
enum nt98513_ab_kind { NT98513_AB_OTHER=0, NT98513_AB_SPS, NT98513_AB_PPS,
 NT98513_AB_VPS, NT98513_AB_IDR, NT98513_AB_BLA, NT98513_AB_CRA,
 NT98513_AB_OTHER_IRAP };
/* All slices borrow input. Caller must keep the complete input alive and
 * immutable while iterator or any slice is used. No allocation/device access.
 * data excludes start code and trailing zero bytes; offset is input-relative. */
struct nt98513_ab_nal { const uint8_t *data; size_t offset,length,start_code_size; };
/* Treat iterator fields as private; initialize with init, never modify them. */
struct nt98513_ab_iter { const uint8_t *data; size_t size,next; int started,terminal; };
struct nt98513_ab_header { unsigned type,ref_idc,layer_id,temporal_id_plus1;
 enum nt98513_ab_kind kind; };
#define NT98513_AB_SPS_BIT 1u
#define NT98513_AB_PPS_BIT 2u
#define NT98513_AB_VPS_BIT 4u
struct nt98513_ab_config {
 struct nt98513_ab_nal sps,pps,vps; /* first occurrence, never silently replaced */
 size_t nal_count,sps_count,pps_count,vps_count,idr_count,bla_count,cra_count,other_irap_count;
 unsigned missing,duplicates; int complete; /* complete means scan reached END */
};
/* Empty input is END. Nonempty all-zero input or nonzero leading junk is
 * FRAMING. Leading/trailing zero bytes are allowed around actual NALs.
 * Consecutive start codes and a final bare start code are EMPTY errors.
 * Errors are sticky. The iterator accepts mixed 3/4-byte prefixes and does
 * not validate RBSP/emulation prevention or establish access-unit boundaries. */
int nt98513_ab_init(struct nt98513_ab_iter *,const uint8_t *,size_t);
int nt98513_ab_next(struct nt98513_ab_iter *,struct nt98513_ab_nal *);
/* Validates fixed header only, not payload semantics. Header-only NALs are
 * accepted. H264 types5/7/8/13/15 require nonzero reference IDC;
 * types6/9..12 require zero. Extension headers14/20/21 and unspecified0/24..31 return
 * UNSUPPORTED. HEVC layer63 is UNSUPPORTED; temporal_id_plus1 must be nonzero and
 * must be1 for IRAP16..23,VPS,SPS,EOS,EOB; TSA and base-layer STSA
 * require temporal_id_plus1>1, EOB requires layer0. Cross-NAL constraints
 * are not validated. Reserved HEVC types retain their type;22/23 are OTHER_IRAP.
 * Header fields/classification never prove a decodable configuration/frame. */
int nt98513_ab_classify(enum nt98513_ab_codec,const struct nt98513_ab_nal *,struct nt98513_ab_header *);
/* Collects over the entire supplied buffer, possibly multiple access units.
 * max_nals must be1..4096. Counts duplicates by NAL type, not parameter-set ID
 * or byte equality: multiple legitimate parameter sets still count. Missing
 * bits and first slices describe the parsed prefix even on error; complete=0.
 * OK with missing/duplicate sets is permitted and explicitly reported. */
int nt98513_ab_collect(enum nt98513_ab_codec,const uint8_t *,size_t,size_t,struct nt98513_ab_config *);
#endif
