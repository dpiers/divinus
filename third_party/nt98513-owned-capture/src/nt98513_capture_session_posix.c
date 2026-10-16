/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
#include "nt98513_capture_session.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
struct posix_io { int cancel; };
static int open_io(void *p){(void)p;int fd=open("/dev/nt98513-capture",O_RDONLY|O_NONBLOCK|O_CLOEXEC);return fd<0?-errno:fd;}
static int cancel_io(void *p){struct posix_io*c=p;struct pollfd f={c->cancel,POLLIN,0};int n=poll(&f,1,0);if(n<0)return errno==EINTR?0:-errno;if(f.revents&POLLNVAL)return -EBADF;return !!(f.revents&(POLLIN|POLLHUP|POLLERR));}
static int wait_io(void *p,int fd){struct posix_io*c=p;struct pollfd f[2]={{fd,POLLIN,0},{c->cancel,POLLIN,0}};int n=poll(f,2,-1);if(n<0)return -errno;if(f[1].revents&POLLNVAL)return -EBADF;if(f[1].revents&(POLLIN|POLLHUP|POLLERR))return NT98513_SESSION_STOP;return ((f[0].revents&POLLIN)?NT98513_SESSION_DATA:0)|((f[0].revents&(POLLERR|POLLHUP|POLLNVAL))?NT98513_SESSION_ERROR:0);}
static int64_t read_io(void*p,int fd,void*b,size_t n){(void)p;ssize_t r=read(fd,b,n);return r<0?-errno:r;}
static int config_io(void*p,int fd,struct nt_oc_config*c){(void)p;return ioctl(fd,NT_OC_GET_CONFIG,c)<0?-errno:0;}
static int clock_io(void*p,uint64_t*t){(void)p;struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts)<0)return -errno;if(ts.tv_sec<0||(uint64_t)ts.tv_sec>NT98513_DIVINUS_MAX_TIMESTAMP_US/1000000)return -EOVERFLOW;*t=(uint64_t)ts.tv_sec*1000000+(uint64_t)ts.tv_nsec/1000;return 0;}
static int close_io(void*p,int fd){(void)p;return close(fd)<0?-errno:0;}
int nt98513_capture_session_posix(int cancel_fd,const struct nt98513_session_sink*s,
 void*c,unsigned char*b,size_t n,struct nt98513_session_result*r)
{
 static const struct nt98513_session_io io={open_io,wait_io,cancel_io,read_io,config_io,clock_io,close_io};
 struct posix_io context={cancel_fd};
 if(cancel_fd<0){if(r){*r=(struct nt98513_session_result){0};r->status=NT98513_SESSION_ARGUMENT;}return NT98513_SESSION_ARGUMENT;}
 return nt98513_capture_session_run(&io,&context,s,c,b,n,r);
}
