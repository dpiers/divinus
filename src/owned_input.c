#ifdef DIVINUS_OWNED_INPUT
/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "owned_input.h"
#include "owned_config.h"
#include "rtsp/rtsp_server.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
struct runtime {
    struct owned_config config;
    int stop[2],done[2];
    atomic_int stopping,first_error;
    rtsp_handle server;
    /* Exclusively capture worker; joined before freeing context. */
    int started, result;
};
static volatile sig_atomic_t signal_seen;
static volatile sig_atomic_t signal_fd=-1;
static void on_signal(int signo) {
    int saved=errno;
    signal_seen=signo;
    if (signal_fd>=0) {
        char b=1;
        (void)write(signal_fd,&b,1);
    }
    errno=saved;
}

static void wake(int fd) {
    char b=1;
    (void)write(fd,&b,1);
}

static int stop_requested(void *p) {
    return atomic_load(&((struct runtime*)p)->stopping);
}

static void failed(void *p,int error) {
    struct runtime*r=p;
    int expected=0;
    if (error>=0)error=-EIO;
    atomic_compare_exchange_strong(&r->first_error,&expected,error);
    atomic_store(&r->stopping,1);
    wake(r->stop[1]);
}
/* Portable timing needed by real RTSP/RTP (not sensor/HAL identification). */ unsigned int millis(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t))return 0;
    return (unsigned int)((uint64_t)t.tv_sec*1000+t.tv_nsec/1000000);
}
/* A failed join leaves possibly-live threads holding this context. It cannot
* become a recoverable status followed by freeing pointers/descriptors. */ static _Noreturn void terminal_join_failure(void) {
    static const char message[]="owned: TERMINAL join failure; no resource teardown\n";
    (void)write(STDERR_FILENO,message,sizeof(message)-1);
    _Exit(EXIT_FAILURE);
}

static int reset_sink(void *p,uint32_t epoch,uint32_t generation) {
    struct runtime*r=p;
    if (r->server) {
        int rc=rtsp_owned_finish(r->server);
        if (rc)terminal_join_failure();
        r->server=NULL;
        r->started=0;
    }
    if (stop_requested(r))return -ECANCELED;
    struct rtsp_owned_options o= {
        .bind_address=r->config.bind_address,.user=r->config.user,.pass=r->config.pass,.port=r->config.port,.should_stop=stop_requested,.failed=failed,.context=r
    };
    int rc=rtsp_owned_prepare(&o,&r->server);
    if (!rc)fprintf(stderr,"owned: PREPARED epoch=%u generation=%u\n",epoch,generation);
    return rc;
}

static int frame_sink(void *p,hal_vidcodec codec,hal_vidstream*s,const struct nt98513_divinus_observations*o) {
    struct runtime*r=p;
    if (codec!=HAL_VIDCODEC_H264||!r->server)return -ENOTSUP;
    if (stop_requested(r))return -ECANCELED;
    if (!r->started&&(!o->sps_count||!o->pps_count))return -ENODATA;
    int rc=rtp_send_h26x(r->server,s,0);
    if (rc)return stop_requested(r)?-ECANCELED:rc;
    if (!r->started) {
        if (stop_requested(r))return -ECANCELED;
        unsigned short port=0;
        rc=rtsp_owned_start(r->server,&port);
        if (rc)return rc;
        r->started=1;
        fprintf(stderr,"owned: LISTEN port=%u timestamp=linux-monotonic-arrival\n",port);
    }
    return 0;
}

static void *capture_worker(void *p) {
    struct runtime*r=p;
    unsigned char*b=malloc(NT98513_SESSION_BUFFER_BYTES);
    int rc=-ENOMEM;
    struct nt98513_session_result result = {0};
    if (b) {
        const struct nt98513_session_sink sink= {
            reset_sink,frame_sink
        };
        rc=owned_capture_run(r->stop[0],&sink,r,b,NT98513_SESSION_BUFFER_BYTES,&result);
    }
    if (rc==NT98513_SESSION_SINK&&result.sink_status==-ECANCELED&&stop_requested(r))rc=0;
    if (r->server) {
        int e=rtsp_owned_finish(r->server);
        if (e)terminal_join_failure();
        r->server=NULL;
    }
    free(b);
    int failure=atomic_load(&r->first_error);
    if (failure)rc=failure;
    r->result=rc;
    fprintf(stderr,"owned: %s status=%d io=%d sink=%d consumed=%llu callbacks=%llu stale=%llu\n",rc?"FAILED":"STOPPED",rc,result.io_error,result.sink_status,(unsigned long long)result.records_consumed,(unsigned long long)result.frames_delivered,(unsigned long long)result.stale_records_discarded);
    wake(r->done[1]);
    return NULL;
}

static int make_pipe(int fd[2]) {
    if (pipe(fd)<0)return -errno;
    for (int i=0;i<2;i++)if (fcntl(fd[i],F_SETFD,FD_CLOEXEC)<0||fcntl(fd[i],F_SETFL,O_NONBLOCK)<0) {
        int e=errno;
        close(fd[0]);
        close(fd[1]);
        fd[0]=fd[1]=-1;
        return -e;
    }
    return 0;
}

int owned_input_main(int argc,char **argv) {
    if (argc!=5||strcmp(argv[1],"--input")||strcmp(argv[2],"nt98513-owned")||strcmp(argv[3],"--config")) {
        fprintf(stderr,"Usage: %s --input nt98513-owned --config /absolute/owned.conf\nOnly owned H264, authenticated TCP RTSP and arrival timestamps are supported.\n",argv[0]);
        return 2;
    }
    struct runtime r = {.stop = {-1, -1}, .done = {-1, -1}};
    int signals[2] = {-1, -1};
    int rc = owned_config_load(argv[4], &r.config);
    if (rc) {
        fprintf(stderr,"owned: configuration rejected (%d); no service started\n",rc);
        return 2;
    }
    if ((rc=make_pipe(r.stop))||(rc=make_pipe(r.done))||(rc=make_pipe(signals)))goto cleanup;
    atomic_init(&r.stopping,0);
    atomic_init(&r.first_error,0);
    signal_seen=0;
    signal_fd=signals[1];
    struct sigaction sa = {0};
    sa.sa_handler=on_signal;
    sigemptyset(&sa.sa_mask);
    int sigs[]= {
        SIGTERM,SIGINT,SIGQUIT,SIGHUP
    };
    for (unsigned i=0;i<sizeof(sigs)/sizeof(*sigs);i++)if (sigaction(sigs[i],&sa,NULL)) {
        rc=-errno;
        goto cleanup;
    }
    sa.sa_handler=SIG_IGN;
    if (sigaction(SIGPIPE,&sa,NULL)) {
        rc=-errno;
        goto cleanup;
    }
    fprintf(stderr,"owned: source=%s base=%s\n", OWNED_BUILD_ID, OWNED_BASE_REV);
    fprintf(stderr,"owned: STARTING (endpoint must already be configured; no SDK initialization)\n");
    pthread_t worker;
    rc=pthread_create(&worker,NULL,capture_worker,&r);
    if (rc) {
        rc=-rc;
        goto cleanup;
    }
    struct pollfd f[2] = {
        {r.done[0], POLLIN, 0},
        {signals[0], POLLIN, 0}
    };
    for (;;) {
        if (signal_seen) {
            atomic_store(&r.stopping,1);
            wake(r.stop[1]);
        }
        int n=poll(f,2,-1);
        if (n<0) {
            if (errno==EINTR)continue;
            rc=-errno;
            atomic_store(&r.stopping,1);
            wake(r.stop[1]);
            break;
        }
        if (f[1].revents) {
            char bytes[64];
            while (read(signals[0],bytes,sizeof(bytes))>0) {
            }
            atomic_store(&r.stopping,1);
            wake(r.stop[1]);
        }
        if (f[0].revents)break;
    }
    {
        int e=pthread_join(worker,NULL);
        if (e)terminal_join_failure();
        if (!rc)rc=r.result;
    }
    cleanup: signal_fd=-1;
    for (int i=0;i<2;i++) {
        if (r.stop[i]>=0)close(r.stop[i]);
        if (r.done[i]>=0)close(r.done[i]);
        if (signals[i]>=0)close(signals[i]);
    }
    if (rc)fprintf(stderr,"owned: exit failure (%d)\n",rc);
    return rc?1:0;
}
#endif
