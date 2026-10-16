/* SPDX-License-Identifier: MIT */
#ifndef DIVINUS_OWNED_CONFIG_H
#define DIVINUS_OWNED_CONFIG_H
#include <stdint.h>
struct owned_config { char bind_address[16],user[32],pass[32]; uint16_t port; };
/* Exact supported keys only. Returns zero or negative errno, never logs secrets. */
int owned_config_load(const char *path,struct owned_config *out);
#endif
