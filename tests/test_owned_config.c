#define _POSIX_C_SOURCE 200809L
#include "owned_config.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static const char good[]="bind=127.0.0.1\nport=0\nuser=owner\npassword=not-default-test-secret\ntimestamp=linux-monotonic-arrival\n";
static void put(const char*p,const char*s){FILE*f=fopen(p,"w");assert(f);assert(fwrite(s,1,strlen(s),f)==strlen(s));assert(fclose(f)==0);}
int main(void){char path[]="/tmp/divinus-owned-config-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);struct owned_config c;put(path,good);assert(owned_config_load(path,&c)==0&&c.port==0&&!strcmp(c.user,"owner"));const char*bad[]={"audio=true\n","port=1\nport=2\n","bind=bogus\n","user=\n","password=\n","timestamp=capture\n","port=-1\n","port=65536\n","sensor=GC2053\n","idr=true\n"};for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){char s[1024];snprintf(s,sizeof(s),"%s%s",good,bad[i]);put(path,s);assert(owned_config_load(path,&c)<0);}put(path,good);FILE *extra=fopen(path,"a");assert(extra);assert(fwrite("\0hidden",1,7,extra)==7);assert(fclose(extra)==0);assert(owned_config_load(path,&c)<0);put(path,good);chmod(path,0644);assert(owned_config_load(path,&c)<0);chmod(path,0600);assert(unlink(path)==0);assert(mkfifo(path,0600)==0);assert(owned_config_load(path,&c)<0);assert(unlink(path)==0);assert(owned_config_load(path,&c)<0);puts("owned config rejection tests passed");}
