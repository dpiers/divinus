/* Only the capture-device open/read/ioctl boundary is substituted. */
#define _POSIX_C_SOURCE 200809L
#include "nt98513_capture_session.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "owned_h264_fixture.h"
static int device_fd=-1;
static uint32_t generation;
int owned_test_device_open(const char *path,int flags,...)
{
 assert(!strcmp(path,"/dev/nt98513-capture"));
 assert(flags==(O_RDONLY|O_NONBLOCK|O_CLOEXEC));
 const char *e=getenv("OWNED_FIXTURE_FD");
 if(!e){errno=ENODEV;return -1;}
 device_fd=dup(atoi(e));
 if(device_fd<0)return -1;
 if(fcntl(device_fd,F_SETFL,O_NONBLOCK)<0||fcntl(device_fd,F_SETFD,FD_CLOEXEC)<0){
  int saved=errno;close(device_fd);device_fd=-1;errno=saved;
 }
 return device_fd;
}
ssize_t owned_test_device_read(int fd,void *b,size_t n)
{
 assert(fd==device_fd);
 unsigned char command[8];ssize_t got=read(fd,command,sizeof(command));
 if(got<0)return -1;
 if(got!=8){errno=EIO;return -1;}
 uint32_t operation;memcpy(&generation,command,4);memcpy(&operation,command+4,4);
 if(operation){errno=EIO;return -1;}
 struct nt_oc_frame_header h={NT_OC_FRAME_MAGIC,NT_OC_ABI,sizeof(h264_small)-34,9,generation,77,0,NT_OC_FLAG_TIMESTAMP_UNAVAILABLE};
 if(n<sizeof(h)+h.bytes){errno=EMSGSIZE;return -1;}
 memcpy(b,&h,sizeof(h));memcpy((unsigned char*)b+sizeof(h),h264_small+34,h.bytes);
 return sizeof(h)+h.bytes;
}
int owned_test_device_ioctl(int fd,unsigned long operation,...)
{
 assert(fd==device_fd&&operation==NT_OC_GET_CONFIG);
 va_list ap;va_start(ap,operation);struct nt_oc_config *c=va_arg(ap,struct nt_oc_config*);va_end(ap);
 *c=(struct nt_oc_config){.abi=1,.generation=generation,.bytes=34,.epoch=9};
 memcpy(c->data,h264_small,34);if(generation==2)c->data[7]=0x0b;
 return 0;
}
