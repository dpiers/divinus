/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef NT98513_CAPTURE_UAPI_H
#define NT98513_CAPTURE_UAPI_H
#include <linux/types.h>
#include <linux/ioctl.h>
#define NT_OC_ABI 1
#define NT_OC_FLAG_TIMESTAMP_UNAVAILABLE 1u
#define NT_OC_FRAME_MAGIC 0x4f434652
struct nt_oc_frame_header {
 __u32 magic, abi, bytes, epoch, generation, token, dropped, flags;
}; /* flags: NT_OC_FLAG_TIMESTAMP_UNAVAILABLE; frame-kind also unavailable. Followed by owned AU. */
struct nt_oc_config { __u32 abi, generation, bytes, epoch; __u8 data[512]; };
struct nt_oc_status { __u32 abi, phase, epoch, generation, maximum, dropped, transport_fault, reserved; };
#define NT_OC_GET_CONFIG _IOR('O',1,struct nt_oc_config)
#define NT_OC_GET_STATUS _IOR('O',2,struct nt_oc_status)
#endif
