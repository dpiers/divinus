#include "rtsp/rtsp.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#undef CHECK
static atomic_uint checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);abort();}}while(0)
static atomic_int stopping;
static int stopped(void *v){(void)v;return atomic_load(&stopping);}
struct reader {int fd; size_t expected,got;};
static void *read_all(void *v){struct reader *r=v;unsigned char b[1024];while(r->got<r->expected){ssize_t n=read(r->fd,b,sizeof(b));if(n>0){for(ssize_t i=0;i<n;i++)CHECK(b[i]==0x5a);r->got+=n;}else if(n==0)break;else if(errno!=EINTR)abort();}return NULL;}
static void *cancel(void *v){(void)v;usleep(40000);atomic_store(&stopping,1);return NULL;}
static void sockets(int fd[2]){CHECK(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);CHECK(fcntl(fd[0],F_SETFL,O_NONBLOCK)==0);int small=1024;CHECK(setsockopt(fd[0],SOL_SOCKET,SO_SNDBUF,&small,sizeof(small))==0);
#ifdef SO_NOSIGPIPE
int one=1;CHECK(setsockopt(fd[0],SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one))==0);
#endif
}
int main(void){struct rtsp_owned_options o={"127.0.0.1","u","p",0,stopped,NULL,NULL};rtsp_handle h;CHECK(rtsp_owned_prepare(&o,&h)==0);size_t size=4*1024*1024;unsigned char *buf=malloc(size);CHECK(buf);memset(buf,0x5a,size);int fd[2];sockets(fd);
 fprintf(stderr,"success case\n");struct reader r={fd[1],size,0};pthread_t thread;CHECK(pthread_create(&thread,NULL,read_all,&r)==0);CHECK(rtsp_owned_write(h,fd[0],buf,size,rtsp_owned_now_ms()+5000)==0);CHECK(pthread_join(thread,NULL)==0);CHECK(r.got==size);close(fd[0]);close(fd[1]);
 fprintf(stderr,"timeout case\n");sockets(fd);int64_t before=rtsp_owned_now_ms();CHECK(rtsp_owned_write(h,fd[0],buf,size,before+100)==-ETIMEDOUT);CHECK(rtsp_owned_now_ms()-before>=90&&rtsp_owned_now_ms()-before<500);close(fd[0]);close(fd[1]);
 fprintf(stderr,"cancel case\n");sockets(fd);before=rtsp_owned_now_ms();CHECK(pthread_create(&thread,NULL,cancel,NULL)==0);CHECK(rtsp_owned_write(h,fd[0],buf,size,before+5000)==-ECANCELED);CHECK(pthread_join(thread,NULL)==0);CHECK(rtsp_owned_now_ms()-before<500);close(fd[0]);close(fd[1]);atomic_store(&stopping,0);
 fprintf(stderr,"closed case\n");sockets(fd);close(fd[1]);CHECK(rtsp_owned_write(h,fd[0],buf,size,rtsp_owned_now_ms()+100)==-EPIPE);close(fd[0]);CHECK(rtsp_owned_write(h,-1,buf,1,rtsp_owned_now_ms()+100)==-EBADF);
 CHECK(rtsp_owned_finish(h)==0);free(buf);printf("owned real short/error/timeout/cancel write PASS: %u assertions\n",checks);}
