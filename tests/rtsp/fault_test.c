/* Narrow libc failure injection around the actual production implementation.
 * Successful calls always reach real owned sockets/threads; no protocol mock. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static atomic_int fault,callback_count,callback_error;
static int alloc_at,alloc_calls,live;
static void *allocations[64];
static void remember(void *p){if(p){for(unsigned i=0;i<64;i++)if(!allocations[i]){allocations[i]=p;++live;return;}abort();}}
static void *f_calloc(size_t a,size_t b){if(++alloc_calls==alloc_at){errno=ENOMEM;return NULL;}void *p=calloc(a,b);remember(p);return p;}
static void *f_malloc(size_t n){if(++alloc_calls==alloc_at){errno=ENOMEM;return NULL;}void *p=malloc(n);remember(p);return p;}
static void f_free(void *p){if(p){unsigned i;for(i=0;i<64;i++)if(allocations[i]==p){allocations[i]=NULL;--live;break;}assert(i<64);}free(p);}
static int f_pipe(int p[2]){if(fault==1){errno=EMFILE;return -1;}return pipe(p);}
static int f_socket(int a,int b,int c){if(fault==2){errno=EMFILE;return -1;}return socket(a,b,c);}
static int f_bind(int fd,const struct sockaddr *a,socklen_t n){if(fault==3){errno=EADDRINUSE;return -1;}return bind(fd,a,n);}
static int f_listen(int fd,int n){if(fault==4){errno=EIO;return -1;}return listen(fd,n);}
static int f_getsockname(int fd,struct sockaddr *a,socklen_t *n){if(fault==5){errno=EIO;return -1;}return getsockname(fd,a,n);}
static int f_create(pthread_t *t,const pthread_attr_t *a,void*(*fn)(void*),void *v){if(fault==6)return EAGAIN;return pthread_create(t,a,fn,v);}
static int f_join(pthread_t t,void **v){if(fault==7)return EINVAL;return pthread_join(t,v);}
static ssize_t f_send(int fd,const void *p,size_t n,int f){if(fault==8)return 0;if(fault==9){fault=0;errno=EINTR;return -1;}return send(fd,p,n,f);}
static int f_poll(struct pollfd *p,nfds_t n,int ms){if(fault==10){errno=EIO;return -1;}return poll(p,n,ms);}
static int f_fcntl(int fd,int cmd,...){if(fault==11){errno=EIO;return -1;}if(cmd==F_GETFL||cmd==F_GETFD)return fcntl(fd,cmd);va_list ap;va_start(ap,cmd);int arg=va_arg(ap,int);va_end(ap);return fcntl(fd,cmd,arg);}
static int f_setsockopt(int fd,int level,int name,const void *v,socklen_t n){if(fault==12){errno=ENOBUFS;return -1;}return setsockopt(fd,level,name,v,n);}
#define calloc f_calloc
#define malloc f_malloc
#define free f_free
#define pipe f_pipe
#define socket f_socket
#define bind f_bind
#define listen f_listen
#define getsockname f_getsockname
#define pthread_create f_create
#define pthread_join f_join
#define send f_send
#define poll f_poll
#define fcntl f_fcntl
#define setsockopt f_setsockopt
#include "rtsp/rtsp.c"
#include "rtsp/rtp.c"
#include "rtsp/mime.c"
#undef calloc
#undef malloc
#undef free
#undef pipe
#undef socket
#undef bind
#undef listen
#undef getsockname
#undef pthread_create
#undef pthread_join
#undef send
#undef poll
#undef fcntl
#undef setsockopt
#undef CHECK
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);abort();}}while(0)
static void failure(void *v,int e){(void)v;atomic_fetch_add(&callback_count,1);atomic_store(&callback_error,e);}
static int fds(void){int n=0;for(int i=0;i<1024;i++)if(fcntl(i,F_GETFD)>=0)n++;return n;}
static void seed(rtsp_handle h){unsigned char data[]={0,0,1,0x67,0x42,0,0x1e,0xab,0,0,1,0x68,0xce};hal_vidpack p={.data=data,.length=sizeof(data)};hal_vidstream s={.pack=&p,.count=1};CHECK(rtp_send_h26x(h,&s,0)==0);}
int main(void){struct rtsp_owned_options o={"127.0.0.1","u","p",0,NULL,NULL,NULL};rtsp_handle h;int baseline=fds();unsigned short port;
 /* Every prepare allocation, including MIME encoder allocation, must unwind. */
 for(alloc_at=1;alloc_at<=22;alloc_at++){alloc_calls=0;int rc=rtsp_owned_prepare(&o,&h);if(rc)CHECK(h==NULL&&rc==-ENOMEM);else CHECK(rtsp_owned_finish(h)==0);CHECK(live==0);CHECK(fds()==baseline);}alloc_at=0;
 int starts[]={1,2,3,4,5,6,11,12};for(unsigned k=0;k<sizeof(starts)/sizeof(*starts);k++){int which=starts[k];CHECK(rtsp_owned_prepare(&o,&h)==0);seed(h);fault=which;CHECK(rtsp_owned_start(h,&port)<0);fault=0;CHECK(rtsp_owned_finish(h)==0);CHECK(live==0&&fds()==baseline);}
 /* A failed join retains resources and can be retried; never frees live state. */
 CHECK(rtsp_owned_prepare(&o,&h)==0);seed(h);CHECK(rtsp_owned_start(h,&port)==0);fault=7;CHECK(rtsp_owned_finish(h)==-EINVAL);CHECK(live>0);fault=0;CHECK(rtsp_owned_finish(h)==0);CHECK(live==0&&fds()==baseline);
 CHECK(rtsp_owned_prepare(&o,&h)==0);int pair[2];CHECK(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);CHECK(fcntl(pair[0],F_SETFL,O_NONBLOCK)==0);char byte='x';fault=8;CHECK(rtsp_owned_write(h,pair[0],&byte,1,rtsp_owned_now_ms()+100)==-EPIPE);fault=9;CHECK(rtsp_owned_write(h,pair[0],&byte,1,rtsp_owned_now_ms()+100)==0);CHECK(read(pair[1],&byte,1)==1&&byte=='x');close(pair[0]);close(pair[1]);CHECK(rtsp_owned_finish(h)==0);CHECK(live==0&&fds()==baseline);
 o.failed=failure;CHECK(rtsp_owned_prepare(&o,&h)==0);seed(h);fault=10;CHECK(rtsp_owned_start(h,&port)==0);
 for(unsigned i=0;i<100&&!atomic_load(&callback_count);i++)usleep(5000);CHECK(atomic_load(&callback_count)==1&&atomic_load(&callback_error)==-EIO);
 fault=0;CHECK(rtsp_owned_finish(h)==0);CHECK(live==0&&fds()==baseline);CHECK(atomic_load(&callback_count)==1);
 printf("owned actual allocation/start/join/zero/EINTR cleanup PASS: %u assertions\n",checks);}
