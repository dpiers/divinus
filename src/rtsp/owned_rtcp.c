/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Daniel Piers
 */
#include "owned_rtcp.h"
#include <string.h>

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

size_t owned_rtcp_serialize_sr_sdes(uint8_t *out, size_t capacity,
    uint32_t ssrc, uint32_t ntp_seconds, uint32_t ntp_fraction,
    uint32_t rtp_timestamp, uint32_t packet_count, uint32_t payload_octet_count,
    const uint8_t *cname, size_t cname_length)
{
    size_t sdes_size, total;
    if (!out || !cname || cname_length == 0 || cname_length > 255)
        return 0;
    /* Header + SSRC + item header + text + END, rounded to a word. */
    sdes_size = (11 + cname_length + 3) & ~(size_t)3;
    total = 28 + sdes_size;
    if (capacity < total)
        return 0;

    /* Copy first so even overlapping caller storage is well defined. */
    memmove(out + 38, cname, cname_length);
    out[0] = 0x80; out[1] = 200; out[2] = 0; out[3] = 6;
    put32(out + 4, ssrc);
    put32(out + 8, ntp_seconds);
    put32(out + 12, ntp_fraction);
    put32(out + 16, rtp_timestamp);
    put32(out + 20, packet_count);
    put32(out + 24, payload_octet_count);
    out[28] = 0x81; out[29] = 202;
    out[30] = 0; out[31] = (uint8_t)(sdes_size / 4 - 1);
    put32(out + 32, ssrc);
    out[36] = 1; out[37] = (uint8_t)cname_length;
    /* END and chunk alignment, not RTCP P-bit padding. */
    memset(out + 38 + cname_length, 0, total - 38 - cname_length);
    return total;
}
