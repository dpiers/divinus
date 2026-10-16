/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NT98513_OC_WIRE_H
#define NT98513_OC_WIRE_H
#ifdef __KERNEL__
#include <linux/types.h>
typedef u32 oc_u32;
typedef u8 oc_u8;
#else
#include <stdint.h>
typedef uint32_t oc_u32;
typedef uint8_t oc_u8;
#endif
#define OC_BYTES 24u
#define OC_VERSION 0x4f430001u
#define OC_MAX_FRAME 0x800000u
#define OC_CONFIG_BYTES 512u
#define OC_TAG 0xc7100000u
#define OC_REPLY 0x00080000u
#define OC_OK 0u
#define OC_DISCARD 1u
#define OC_REJECT 2u
#define OC_BIND 1u
#define OC_CONFIGURE 2u
#define OC_CONFIG_COPY 3u
#define OC_FRAME 4u
#define OC_DRAIN 5u
#define OC_UNBIND 6u
struct oc_message { oc_u32 w[6]; };
int oc_decode(const oc_u8 *bytes, unsigned int length, struct oc_message *m);
void oc_encode(const struct oc_message *m, oc_u8 bytes[OC_BYTES]);
int oc_span(oc_u32 base, oc_u32 size, oc_u32 address, oc_u32 bytes);
#endif
