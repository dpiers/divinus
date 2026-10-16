/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NT98513_OC_READER_CONTRACT_H
#define NT98513_OC_READER_CONTRACT_H
#include "../linux/nt98513_capture_uapi.h"
static inline int nt_oc_config_matches(const struct nt_oc_frame_header *h,const struct nt_oc_config *c)
{
 return c->abi==NT_OC_ABI && c->generation==h->generation && c->epoch==h->epoch && h->epoch && h->generation && c->bytes && c->bytes<=512;
}
#endif
