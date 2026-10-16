#ifdef DIVINUS_OWNED_INPUT
/* SPDX-License-Identifier: MIT */
#include "owned_input.h"
int owned_capture_run(int fd,const struct nt98513_session_sink*s,void*c,
 unsigned char*b,size_t n,struct nt98513_session_result*r)
{return nt98513_capture_session_posix(fd,s,c,b,n,r);}

#endif
