/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Daniel Piers
 */
#ifndef OWNED_RTCP_H
#define OWNED_RTCP_H

#include <stddef.h>
#include <stdint.h>

#define OWNED_RTCP_MAX_COMPOUND_SIZE 296u

/* RFC 3550 SR (no report blocks) followed by one SDES CNAME chunk.
 * Returns bytes written, or zero without modifying output on invalid pointers,
 * CNAME length outside 1..255, or insufficient capacity. CNAME is length-delimited;
 * caller supplies valid UTF-8 and a suitable stable identity. Overlap is allowed.
 * All numeric inputs are host values. NTP and RTP timestamps must refer to the
 * same instant; counts are cumulative RTP packets and payload octets, modulo
 * 2^32. Caller owns clock mapping, accounting, SSRC lifetime and scheduling.
 */
size_t owned_rtcp_serialize_sr_sdes(uint8_t *out, size_t capacity,
    uint32_t ssrc, uint32_t ntp_seconds, uint32_t ntp_fraction,
    uint32_t rtp_timestamp, uint32_t packet_count, uint32_t payload_octet_count,
    const uint8_t *cname, size_t cname_length);

#endif
