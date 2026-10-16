/* Test real private recv/response path over an owned nonblocking socketpair. */
#include "rtsp/rtsp.c"
#include <assert.h>
#include <stdatomic.h>
static atomic_int done;
static void *drain(void *v){int fd=*(int*)v;char b[4096];while(!atomic_load(&done)){usleep(120000);recv(fd,b,sizeof(b),MSG_DONTWAIT);}return NULL;}
int main(void){struct rtsp_owned_options o={"127.0.0.1","u","p",0,NULL,NULL,NULL};rtsp_handle h;assert(rtsp_owned_prepare(&o,&h)==0);
 unsigned char bytes[1024];memset(bytes,0x55,sizeof(bytes));bytes[0]=0x67;bytes[1]=0x42;bytes[2]=0;bytes[3]=0x1e;h->sprop_sps_b64=mime_base64_create((char*)bytes,sizeof(bytes));h->sprop_sps_b16=mime_base16_create((char*)bytes+1,3);h->sprop_pps_b64=mime_base64_create((char*)bytes,4);assert(h->sprop_sps_b64&&h->sprop_sps_b16&&h->sprop_pps_b64);
 int fd[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);assert(owned_fd_flags(fd[0],1)==0);assert(owned_fd_flags(fd[1],1)==0);int small=1024;assert(setsockopt(fd[0],SOL_SOCKET,SO_SNDBUF,&small,sizeof(small))==0);
 struct connection_item_t *c=&h->connections[0];c->client_fd=fd[0];c->con_state=__CON_S_INIT;
 const char *q="DESCRIBE rtsp://127.0.0.1/ RTSP/1.0\r\nCSeq: 1\r\nAuthorization: Basic dTpw\r\n\r\n";char requests[4096]={0};for(unsigned i=0;i<30;i++)strcat(requests,q);assert(send(fd[1],requests,strlen(requests),0)==(ssize_t)strlen(requests));
 pthread_t reader;assert(pthread_create(&reader,NULL,drain,&fd[1])==0);int64_t start=rtsp_owned_now_ms();int rc=owned_read(c);int64_t elapsed=rtsp_owned_now_ms()-start;atomic_store(&done,1);assert(pthread_join(reader,NULL)==0);fprintf(stderr,"pipeline result%d elapsed%lldms\n",rc,(long long)elapsed);assert(rc==-ETIMEDOUT&&elapsed>=150&&elapsed<500);close(fd[1]);assert(rtsp_owned_finish(h)==0);puts("owned whole-recv response deadline PASS");}
