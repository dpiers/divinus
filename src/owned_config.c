/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "owned_config.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int credential(const char *s,int user) {
    size_t n=strlen(s);
    if (!n||n>31)return 0;
    for (size_t i=0;i<n;i++)if ((unsigned char)s[i]<33||(unsigned char)s[i]>126||(user&&s[i]==':'))return 0;
    return 1;
}

int owned_config_load(const char *path,struct owned_config *out) {
    if (!path||path[0]!='/'||!out)return -EINVAL;
    int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    if (fd<0)return -errno;
    struct stat st;
    if (fstat(fd,&st)<0) {
        int e=errno;
        close(fd);
        return -e;
    }
    if (!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&077)||st.st_size>4096) {
        close(fd);
        return -EPERM;
    }
    char data[4097];
    size_t used=0;
    int rc=0;
    while (used<sizeof(data)) {
        ssize_t got=read(fd,data+used,sizeof(data)-used);
        if (got<0) {
            if (errno==EINTR)continue;
            rc=-errno;
            break;
        }
        if (!got)break;
        used+=(size_t)got;
    }
    if (close(fd)&&!rc)rc=-errno;
    if (rc)return rc;
    if (used==sizeof(data)||memchr(data,0,used))return -EINVAL;
    data[used]=0;
    struct owned_config c= {
        0
    };
    unsigned seen=0;
    char *save=NULL;
    for (char *line=strtok_r(data,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
        size_t n=strlen(line);
        if (n>254) {
            rc=-EINVAL;
            break;
        }
        if (!n||line[0]=='#')continue;
        char *v=strchr(line,'=');
        if (!v) {
            rc=-EINVAL;
            break;
        }
        *v++=0;
        unsigned bit=0;
        if (!strcmp(line,"bind")) {
            struct in_addr a;
            bit=1;
            if (strlen(v)>=sizeof(c.bind_address)||inet_pton(AF_INET,v,&a)!=1) {
                rc=-EINVAL;
                break;
            }
            strcpy(c.bind_address,v);
        }
        else if (!strcmp(line,"port")) {
            bit=2;
            char *end;
            for (char*q=v;*q;q++)if (*q<'0'||*q>'9') {
                rc=-EINVAL;
                break;
            }
            if (rc)break;
            errno=0;
            unsigned long p=strtoul(v,&end,10);
            if (!*v||*end||errno||p>65535) {
                rc=-EINVAL;
                break;
            }
            c.port=(uint16_t)p;
        }
        else if (!strcmp(line,"user")) {
            bit=4;
            if (!credential(v,1)) {
                rc=-EINVAL;
                break;
            }
            strcpy(c.user,v);
        }
        else if (!strcmp(line,"password")) {
            bit=8;
            if (!credential(v,0)) {
                rc=-EINVAL;
                break;
            }
            strcpy(c.pass,v);
        }
        else if (!strcmp(line,"timestamp")) {
            bit=16;
            if (strcmp(v,"linux-monotonic-arrival")) {
                rc=-ENOTSUP;
                break;
            }
        }
        else  {
            rc=-ENOTSUP;
            break;
        }
        if (seen&bit) {
            rc=-EINVAL;
            break;
        }
        seen|=bit;
    }
    if (!rc&&seen!=31)rc=-EINVAL;
    if (!rc)*out=c;
    return rc;
}
