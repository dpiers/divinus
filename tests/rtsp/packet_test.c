#define main protocol_main
#include "protocol_test.c"
#undef main
static void exact_read(int fd,unsigned char *buf,size_t n) {size_t off=0;while(off<n){struct pollfd p={fd,POLLIN,0};CHECK(poll(&p,1,2000)>0);ssize_t r=recv(fd,buf+off,n-off,0);CHECK(r>0);off+=r;}}
static void packets(int fd,unsigned count,unsigned last_type) {
 unsigned char b[2048];unsigned markers=0;
 for(unsigned i=0;i<count;i++){exact_read(fd,b,4);CHECK(b[0]=='$'&&b[1]==2);unsigned n=((unsigned)b[2]<<8)|b[3];CHECK(n<=sizeof(b));exact_read(fd,b,n);unsigned mark=b[1]>>7;markers+=mark;CHECK(mark==(i==count-1));if(i==count-1)CHECK((b[12]&31)==last_type);}
 CHECK(markers==1);
}
int main(void){struct rtsp_owned_options o={"127.0.0.1","u","p",0,stopped,failure,NULL};rtsp_handle h;unsigned short port;
 CHECK(rtsp_owned_prepare(&o,&h)==0);seed(h,0x42);CHECK(rtsp_owned_start(h,&port)==0);int fd=connect_to(port);setup_play(fd);
 unsigned char multi[]={0,0,0,1,0x65,1,2,3,4,0,0,0,1,0x41,1,2,3,4};hal_vidpack p={.data=multi,.length=sizeof(multi),.timestamp=1000000};hal_vidstream s={.pack=&p,.count=1};CHECK(rtp_send_h26x(h,&s,0)==0);packets(fd,2,1);
 unsigned char short_nals[]={0,0,0,1,9,0xf0,0,0,0,1,0x41,0x80};p.data=short_nals;p.length=sizeof(short_nals);CHECK(rtp_send_h26x(h,&s,0)==0);packets(fd,2,1);
 unsigned char large[8010];memset(large,0x55,sizeof(large));large[0]=large[1]=large[2]=0;large[3]=1;large[4]=0x65;large[4005]=large[4006]=large[4007]=0;large[4008]=1;large[4009]=0x41;p.data=large;p.length=sizeof(large);CHECK(rtp_send_h26x(h,&s,0)==0);packets(fd,6,28);
 CHECK(rtsp_owned_finish(h)==0);check_eof(fd);printf("owned packet marker/short NAL PASS: %u assertions\n",checks);}
