#include "rtsp/rtsp.h"
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static atomic_int stop_flag, failed;
static atomic_uint checks;
#undef CHECK
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); abort(); } } while(0)
static int stopped(void *v) { (void)v; return atomic_load(&stop_flag); }
static void failure(void *v,int e) { (void)v; atomic_store(&failed,e); }
static void seed(rtsp_handle h,unsigned char profile) {
 unsigned char data[]={0,0,0,1,0x67,0x42,0,0x1e,0xab,0,0,0,1,0x68,0xce,6,0xe2,0,0,0,1,0x65,0x88,0x84,0x21}; data[5]=profile;
 hal_vidpack p={.data=data,.length=sizeof(data),.timestamp=1000000};
 hal_vidstream s={.pack=&p,.count=1}; CHECK(rtp_send_h26x(h,&s,0)==0);
}
static int connect_to(unsigned short port) {
 int fd=socket(AF_INET,SOCK_STREAM,0); CHECK(fd>=0);
 struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(port)}; CHECK(inet_pton(AF_INET,"127.0.0.1",&a.sin_addr)==1);
 CHECK(connect(fd,(void*)&a,sizeof(a))==0); return fd;
}
static void read_response(int fd,char *b,size_t capacity,const char *expected) {
 size_t n=0,total=0;
 do { struct pollfd p={fd,POLLIN,0}; CHECK(poll(&p,1,2000)>0); ssize_t r=recv(fd,b+n,capacity-n-1,0); CHECK(r>0); n+=(size_t)r; b[n]=0;
 char *end=strstr(b,"\r\n\r\n"); if(end) { total=(size_t)(end-b)+4; char *len=strstr(b,"Content-Length: "); if(len&&len<end) total+=strtoul(len+16,NULL,10); }
 } while(!total||n<total);
 CHECK(n==total); if(!strstr(b,expected)) fprintf(stderr,"expected %s, got %s\n",expected,b); CHECK(strstr(b,expected));
}
static void exchange(int fd,const char *request,const char *expected) {
 CHECK(send(fd,request,strlen(request),0)==(ssize_t)strlen(request)); char b[8192]; read_response(fd,b,sizeof(b),expected);
}
static void command(int fd,const char *method,const char *track,const char *extra,const char *expected,char *reply) {
 char req[4096]; snprintf(req,sizeof(req),"%s rtsp://127.0.0.1/%s RTSP/1.0\r\nCSeq: 1\r\nAuthorization: Basic dTpw\r\n%s\r\n",method,track,extra);
 CHECK(send(fd,req,strlen(req),0)==(ssize_t)strlen(req)); char local[8192]; read_response(fd,reply?reply:local,8192,expected);
}
static void setup_play(int fd) {
 char reply[8192]; command(fd,"SETUP","track=0","Transport: RTP/AVP/TCP;unicast;interleaved=2-3\r\n","200 OK",reply);
 CHECK(strstr(reply,";timeout=60\r\n")); char *s=strstr(reply,"Session: "); CHECK(s); unsigned long long id=0; CHECK(sscanf(s,"Session: %llx",&id)==1&&id);
 command(fd,"PLAY","","Session: 0\r\n","454 Session Not Found",NULL);
 char hdr[80]; snprintf(hdr,sizeof(hdr),"Session: %llx\r\n",id); command(fd,"PLAY","",hdr,"200 OK",NULL);
}
static void receive_rtp(int fd) {
 unsigned char b[256]; size_t n=0;
 /* SPS5 + PPS4 + IDR4, each with 4-byte interleaving and 12-byte RTP header. */
 while(n<61) {struct pollfd p={fd,POLLIN,0};CHECK(poll(&p,1,2000)>0);ssize_t r=recv(fd,b+n,sizeof(b)-n,0);CHECK(r>0);n+=(size_t)r;}
 CHECK(n==61); size_t off=0;unsigned seq=0;
 for(unsigned i=0;i<3;i++) { CHECK(b[off]=='$'&&b[off+1]==2); unsigned len=((unsigned)b[off+2]<<8)|b[off+3]; CHECK(len==(i?16:17)); CHECK(b[off+4]==0x80); CHECK((b[off+5]&127)==96); CHECK((b[off+5]>>7)==(i==2)); unsigned q=((unsigned)b[off+6]<<8)|b[off+7]; if(i)CHECK(q==((seq+1)&65535));seq=q; CHECK(b[off+8]==0&&b[off+9]==1&&b[off+10]==0x5f&&b[off+11]==0x90); CHECK((b[off+16]&31)==(i==0?7:i==1?8:5)); off+=len+4; }
}
static void check_eof(int fd) { struct pollfd p={fd,POLLIN,0}; CHECK(poll(&p,1,2000)>0);char b;ssize_t n=recv(fd,&b,1,0);CHECK(n==0||(n<0&&errno==ECONNRESET));close(fd); }
int main(void) {
 struct rtsp_owned_options o={"127.0.0.1","u","p",0,stopped,failure,NULL}; rtsp_handle h=NULL;
 struct rtsp_owned_options bad=o; bad.pass=""; CHECK(rtsp_owned_prepare(&bad,&h)==-EINVAL&&h==NULL);
 bad=o;bad.user="u:x";CHECK(rtsp_owned_prepare(&bad,&h)==-EINVAL);bad=o;bad.bind_address="invalid";CHECK(rtsp_owned_prepare(&bad,&h)==-EINVAL);
 bad=o;bad.pass="01234567890123456789012345678901";CHECK(rtsp_owned_prepare(&bad,&h)==-EINVAL);
 CHECK(rtsp_owned_prepare(&o,&h)==0); unsigned short port=0;
 CHECK(rtsp_owned_start(h,&port)==-ENODATA); seed(h,0x42); CHECK(rtsp_owned_start(h,&port)==0&&port);
 unsigned short other;CHECK(rtsp_owned_start(h,&other)==-EALREADY);
 /* Bind conflict leaves a fully finishable prepared object. */
 rtsp_handle conflict;bad=o;bad.port=port;CHECK(rtsp_owned_prepare(&bad,&conflict)==0);seed(conflict,0x42);CHECK(rtsp_owned_start(conflict,&other)==-EADDRINUSE);CHECK(rtsp_owned_finish(conflict)==0);
 int fd=connect_to(port);
 exchange(fd,"OPTIONS rtsp://127.0.0.1/ RTSP/1.0\r\nCSeq: 1\r\n\r\n","401 Unauthorized");
 const char *auths[]={"Basic dTpwgarbage","Basic dTpw ","Basic dTpw=","Basic dTpq","Basic d Tpw","Basic !dTpw","Basic","Bearer dTpw"};
 for(unsigned i=0;i<sizeof(auths)/sizeof(*auths);i++) { char q[512];snprintf(q,sizeof(q),"OPTIONS rtsp://127.0.0.1/ RTSP/1.0\r\nCSeq: 1\r\nAuthorization: %s\r\n\r\n",auths[i]);exchange(fd,q,"401 Unauthorized"); }
 command(fd,"DESCRIBE","","","sprop-parameter-sets=Z0IAHqs=,aM4G4g==",NULL);
 command(fd,"SETUP","track=1","Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n","404 Not Found",NULL);
 command(fd,"SETUP","track=99999999999999999999","Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n","404 Not Found",NULL);
 const char *transports[]={"RTP/AVP;unicast;client_port=5000-5001","RTP/AVP/TCP;unicast;interleaved=256-257","RTP/AVP/TCP;unicast;interleaved=0-0","RTP/AVP/TCP;unicast;interleaved=0-1junk","RTP/AVP/TCP;multicast;interleaved=0-1"};
 for(unsigned i=0;i<sizeof(transports)/sizeof(*transports);i++){char q[256];snprintf(q,sizeof(q),"Transport: %s\r\n",transports[i]);command(fd,"SETUP","track=0",q,"461 Unsupported Transport",NULL);}
 command(fd,"PLAY","","","455 Method Not Valid in This State",NULL);
 command(fd,"RECORD","","","405 Method Not Allowed",NULL);
 /* Genuine partial header: a read must not dispatch or discard it. */
 const char *partial="OPTIONS rtsp://127.0.0.1/ RTSP/1.0\r\nCSeq: 8\r\nAuthorization: Ba";
 CHECK(send(fd,partial,strlen(partial),0)==(ssize_t)strlen(partial));struct pollfd wait={fd,POLLIN,0};CHECK(poll(&wait,1,80)==0);
 exchange(fd,"sic dTpw\r\n\r\n","200 OK");

 exchange(fd,"OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nAuthorization: Basic dTpw\r\n\r\n","200 OK");
 command(fd,"PLAY","","Require: unsupported-option\r\n","Unsupported: unsupported-option\r\n",NULL);
 command(fd,"PLAY","","Proxy-Require: unsupported-option\r\n","Unsupported: unsupported-option\r\n",NULL);
 command(fd,"PLAY","","Range: npt=500-\r\n","457 Invalid Range",NULL);
 command(fd,"PLAY","","Scale: 2\r\n","406 Not Acceptable",NULL);
 command(fd,"PLAY","","Speed: 2\r\n","406 Not Acceptable",NULL);
 /* Two requests in one write must produce two independent authenticated replies. */
 const char *q="OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nAuthorization: Basic dTpw\r\n\r\n";
 char pipeline[1024];snprintf(pipeline,sizeof(pipeline),"%s%s",q,q);CHECK(send(fd,pipeline,strlen(pipeline),0)==(ssize_t)strlen(pipeline));
 char responses[4096]={0};size_t got=0;unsigned responses_count=0;
 while(responses_count<2){struct pollfd f={fd,POLLIN,0};CHECK(poll(&f,1,2000)>0);ssize_t n=recv(fd,responses+got,sizeof(responses)-got-1,0);CHECK(n>0);got+=n;responses[got]=0;responses_count=0;char *p=responses;while((p=strstr(p,"RTSP/1.0 200 OK"))){responses_count++;p++;}}
 CHECK(responses_count==2);
 setup_play(fd);seed(h,0x42);receive_rtp(fd);
 /* Split inbound RTCP is consumed without corrupting the next request. */
 unsigned char rtcp[]={ '$',3,0,5,1,2,3,4,5 };CHECK(send(fd,rtcp,3,0)==3);CHECK(send(fd,rtcp+3,6,0)==6);command(fd,"OPTIONS","","","200 OK",NULL);
 int reordered=connect_to(port);command(reordered,"SETUP","track=0","Transport: RTP/AVP/TCP;mode=\"PLAY\";interleaved=4-5;unicast\r\n","200 OK",NULL);close(reordered);
 CHECK(rtp_send_pcma(h,(unsigned char*)"x",1)==-ENOTSUP);CHECK(rtp_send_pcmu(h,(unsigned char*)"x",1)==-ENOTSUP);CHECK(rtp_send_mp3(h,(unsigned char*)"x",1)==-ENOTSUP);
 CHECK(rtsp_owned_finish(h)==0);check_eof(fd);CHECK(!atomic_load(&failed));
 /* Generation reset: old clients close; replacement uses new packetizer cache. */
 for(unsigned i=0;i<24;i++) { CHECK(rtsp_owned_prepare(&o,&h)==0);seed(h,0x64);CHECK(rtsp_owned_start(h,&port)==0);fd=connect_to(port);command(fd,"DESCRIBE","","","profile-level-id=64001E",NULL); CHECK(rtsp_owned_finish(h)==0);check_eof(fd); }
 /* Partial request, oversized request and pending clients during shutdown. */
 CHECK(rtsp_owned_prepare(&o,&h)==0);seed(h,0x42);CHECK(rtsp_owned_start(h,&port)==0);fd=connect_to(port);CHECK(send(fd,"OPT",3,0)==3);CHECK(rtsp_owned_finish(h)==0);check_eof(fd);
 CHECK(rtsp_owned_prepare(&o,&h)==0);seed(h,0x42);CHECK(rtsp_owned_start(h,&port)==0);fd=connect_to(port);char huge[5000];memset(huge,'X',sizeof(huge));CHECK(send(fd,huge,sizeof(huge),0)==sizeof(huge));check_eof(fd);CHECK(rtsp_owned_finish(h)==0);
 CHECK(rtsp_owned_prepare(&o,&h)==0);atomic_store(&stop_flag,1);CHECK(rtsp_owned_start(h,&port)==-ECANCELED);CHECK(rtsp_owned_finish(h)==0);CHECK(!atomic_load(&failed));
 printf("owned RTSP protocol/lifecycle PASS: %u assertions\n",checks);return 0;
}
