/* Exercise the retained legacy request handler with real local socket I/O.
 * IDR is an uncalled assertion boundary; this test only sends OPTIONS. */
#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include "rtsp/rtsp.c"
void request_idr(void) { abort(); }
static void test(const char *value,const char *status) {
 int fd[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
 struct __rtsp_obj_t h={.isAuthOn=1};strcpy(h.user,"u");strcpy(h.pass,"p");
 struct connection_item_t c={.client_fd=fd[0],.con_state=__CON_S_INIT};pthread_mutex_init(&c.write_mutex,NULL);
 c.fp_tcp_read=fdopen(dup(fd[0]),"r");c.fp_tcp_write=fdopen(dup(fd[0]),"w");assert(c.fp_tcp_read&&c.fp_tcp_write);
 char b[1024];int n=snprintf(b,sizeof(b),"OPTIONS rtsp://127.0.0.1/ RTSP/1.0\r\nCSeq: 1\r\nAuthorization: %s\r\n\r\n",value);assert(write(fd[1],b,n)==n);
 struct sock_select_t select={.h_rtsp=&h};FD_ZERO(&select.rfds);FD_SET(fd[0],&select.rfds);assert(__message_proc_sock(&c.list_entry,&select)==0);
 n=read(fd[1],b,sizeof(b)-1);assert(n>0);b[n]=0;if(!strstr(b,status))fprintf(stderr,"%s => %s\n",value,b);assert(strstr(b,status));
 fclose(c.fp_tcp_read);fclose(c.fp_tcp_write);close(fd[0]);close(fd[1]);pthread_mutex_destroy(&c.write_mutex);
}
int main(void){test("Basic dTpw","200 OK");test("Basic dTpwgarbage","401 Unauthorized");test("Basic dTpw ","401 Unauthorized");test("Basic dTpq","401 Unauthorized");puts("legacy actual auth handler PASS");}
