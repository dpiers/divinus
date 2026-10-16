#define main protocol_main
#include "protocol_test.c"
#undef main
struct drain { int fd; size_t bytes; };
static void exact(int fd,void *v,size_t size) {unsigned char *p=v;while(size){struct pollfd f={fd,POLLIN,0};CHECK(poll(&f,1,2500)>0);ssize_t n=recv(fd,p,size,0);CHECK(n>0);p+=n;size-=n;}}
static void *drain_rtp(void *v){struct drain *d=v;size_t got=0;unsigned marks=0;while(got<d->bytes){unsigned char b[2048];exact(d->fd,b,4);CHECK(b[0]=='$'&&b[1]==2);size_t n=((unsigned)b[2]<<8)|b[3];CHECK(n>=14&&n<sizeof(b));exact(d->fd,b,n);CHECK((b[12]&31)==28);marks+=b[1]>>7;got+=n-14;}CHECK(got==d->bytes);CHECK(marks==1);return NULL;}
int main(void){struct rtsp_owned_options o={"127.0.0.1","u","p",0,stopped,failure,NULL};rtsp_handle h;unsigned short port;CHECK(rtsp_owned_prepare(&o,&h)==0);seed(h,0x42);CHECK(rtsp_owned_start(h,&port)==0);
 int slow=connect_to(port);setup_play(slow);int healthy=connect_to(port);setup_play(healthy);
 rtsp_lock(h);int small=1024;CHECK(setsockopt(h->connections[0].client_fd,SOL_SOCKET,SO_SNDBUF,&small,sizeof(small))==0);rtsp_unlock(h);
 size_t size=512*1024;unsigned char *data=malloc(size+5);CHECK(data);memset(data,0x55,size+5);memset(data,0,3);data[3]=1;data[4]=0x65;
 hal_vidpack p={.data=data,.length=size+5,.timestamp=1000000};hal_vidstream s={.pack=&p,.count=1};struct drain d={healthy,size};pthread_t thread;CHECK(pthread_create(&thread,NULL,drain_rtp,&d)==0);int64_t before=rtsp_owned_now_ms();CHECK(rtp_send_h26x(h,&s,0)==0);CHECK(rtsp_owned_now_ms()-before<1000);CHECK(pthread_join(thread,NULL)==0);CHECK(!atomic_load(&failed));
 rtsp_lock(h);CHECK(h->connections[0].client_fd==-1);rtsp_unlock(h);close(slow);free(data);
 /* Fresh peer and an abruptly reset peer cannot terminate the healthy stream. */
 int fresh=connect_to(port);setup_play(fresh);struct linger reset={1,0};CHECK(setsockopt(fresh,SOL_SOCKET,SO_LINGER,&reset,sizeof(reset))==0);close(fresh);seed(h,0x42);receive_rtp(healthy);CHECK(!atomic_load(&failed));
 fresh=connect_to(port);setup_play(fresh);seed(h,0x42);receive_rtp(healthy);receive_rtp(fresh);close(fresh);close(healthy);CHECK(rtsp_owned_finish(h)==0);
 /* Never-writing and401-then-idle connections release pre-SETUP slots. */
 CHECK(rtsp_owned_prepare(&o,&h)==0);seed(h,0x42);CHECK(rtsp_owned_start(h,&port)==0);int idle=connect_to(port);int denied=connect_to(port);exchange(denied,"OPTIONS * RTSP/1.0\r\nCSeq: 1\r\n\r\n","401 Unauthorized");
 struct pollfd f={idle,POLLIN,0};CHECK(poll(&f,1,6500)>0);check_eof(idle);check_eof(denied);CHECK(rtsp_owned_finish(h)==0);
 printf("owned peer isolation/reconnect/idle PASS: %u assertions\n",checks);}
