/* SPDX-License-Identifier: MIT */
#ifndef DIVINUS_OWNED_INPUT_H
#define DIVINUS_OWNED_INPUT_H
#include "nt98513_capture_session.h"
int owned_input_main(int argc,char **argv);
/* Production adapter calls accepted POSIX session. Host tests replace only this
 * device adapter with actual session core + source-owned record IO. */
int owned_capture_run(int stop_fd,const struct nt98513_session_sink*,void*,
 unsigned char*,size_t,struct nt98513_session_result*);
#endif
