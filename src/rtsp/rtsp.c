#include <stdarg.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include "rtsp_server.h"
#include "common.h"
#include "rtsp.h"
#include "list.h"
#include "hash.h"
#include "mime.h"
#include "thread.h"
#include "rfc.h"
#include "rtcp.h"
#include "bufpool.h"
#include <netinet/in.h>
#include <sys/ioctl.h>

#ifndef DIVINUS_OWNED_INPUT
extern void request_idr();

/******************************************************************************
 *              PRIVATE DEFINITIONS
 ******************************************************************************/
#define __STR_OPTIONS  "OPTIONS"
#define __STR_AUTH "AUTHORIZATION"
#define __STR_CSEQ  "CSEQ"
#define __STR_DESCRIBE  "DESCRIBE"
#define __STR_SETUP  "SETUP"
#define __STR_PLAY  "PLAY"
#define __STR_TEARDOWN  "TEARDOWN"
#define __STR_TRANSPORT  "TRANSPORT"
#define __STR_CLIENTPORT  "client_port"
#define __STR_INTERLEAVED "interleaved"
#define __STR_RTP_AVP_TCP "RTP/AVP/TCP"
#define __STR_SESSION  "SESSION"
#define __STR_PAUSE "PAUSE"
#define __STR_RECORDING "RECORDING"
#define __STR_RANGE  "RANGE"
#define __SPACE " "

#define __RESPONCE_STR_OK "200 OK"
#define __RESPONCE_STR_BADREQUEST "400 Bad Request"
#define __RESPONCE_STR_METHODINVAL "455 Method Not Valid in This State"
#define __RESPONCE_STR_METHODNOTALLOWED "405 Method Not Allowed"
#define __RESPONCE_STR_MOVEDPERM "301 Moved Permanently"
#define __RESPONCE_STR_SERVERERROR "500 Internal Server Error"
#define __RESPONCE_STR_OPTIONUNSUPPORTED "551 Option not supported"

#define __PARSE_ERROR(p) do {ERR("cannot parse '%s' in %s\n", buf, __FUNCTION__); p->parser_state = __PARSER_S_ERROR;}while(0)

/******************************************************************************
 *              PRIVATE DECLARATION
 ******************************************************************************/
static inline int __bind_rtp(struct connection_item_t *con );
static inline int __bind_rtcp(struct connection_item_t *con );
static inline int __bind_tcp(unsigned short port);

static void __method_auth(struct connection_item_t *p, rtsp_handle h);
static void __method_options(struct connection_item_t *p, rtsp_handle h);
static void __method_describe(struct connection_item_t *p, rtsp_handle h);
static void __method_setup(struct connection_item_t *p, rtsp_handle h);
static void __method_play(struct connection_item_t *p, rtsp_handle h);
static void __method_pause(struct connection_item_t *p, rtsp_handle h);
static void __method_record(struct connection_item_t *p, rtsp_handle h);
static void __method_error(struct connection_item_t *p, rtsp_handle h);

static void *rtspThrFxn(void *v);

static inline int __connection_list_add(bufpool_handle con_pool, struct list_head_t *head,int fd, struct sockaddr_in addr);
static int __connection_reset(void *v);
static inline int __accept_proc_sock(rtsp_handle h, int server_fd, struct sock_select_t *p_socks);
static int __message_proc_sock(struct list_t *e, void *p);
static inline int __set_select_sock(struct list_t *p, void *param);
static inline int __find_fd_max(struct list_head_t *head);

static inline bufpool_handle __connectionpool_create(int num);
static int __connection_is_dead(struct list_t *l);

/******************************************************************************
 *              PRIVATE DATA
 ******************************************************************************/
static struct connection_item_t __connection_pool[RTSP_MAXIMUM_CONNECTIONS] = {};

static struct transfer_item_t __transfer_pool[RTSP_MAXIMUM_CONNECTIONS] = {};

/******************************************************************************
 *              PRIVATE FUNCTIONS
 ******************************************************************************/
static void *__bufgetter_connection(int i)
{
    return (void *)&(__connection_pool[i]);
}

static inline bufpool_handle __connectionpool_create(int num)
{
    bufpool_handle h = bufpool_create(num, (__bufgetter_connection), (__connection_reset), sizeof(struct connection_item_t));

    if (h) {
        int i;
        for(i = 0; i < num; i++) {
            __connection_pool[i].pool = h;
            __connection_pool[i].con_state = __CON_S_DISCONNECTED;
            pthread_mutex_init(&__connection_pool[i].write_mutex, NULL);
        }
    }

    return h;
}

static void *__bufgetter_trans(int i)
{
    return (void *)&(__transfer_pool[i]);
}

static inline bufpool_handle __transpool_create(int num)
{
    bufpool_handle h = bufpool_create(num, (__bufgetter_trans), NULL, sizeof(struct transfer_item_t));

    if (h) {
        int i;
        for(i = 0; i < num; i++) {
            __transfer_pool[i].pool = h;
            __transfer_pool[i].list_entry.cleaner = (__transfer_item_cleaner);
        }
    }

    return h;
}

/******************************************************************************
 *              RESPONSE IMPLEMENTATIONS
 ******************************************************************************/
static int __rtsp_write(struct connection_item_t *p, const char *fmt, ...)
{
    va_list args;
    int ret;

    pthread_mutex_lock(&p->write_mutex);
    va_start(args, fmt);
    ret = vfprintf(p->fp_tcp_write, fmt, args);
    va_end(args);
    fflush(p->fp_tcp_write);
    pthread_mutex_unlock(&p->write_mutex);

    return ret;
}

static void __method_auth(struct connection_item_t *p, rtsp_handle h)
{
    __rtsp_write(p, "RTSP/1.0 401 Unauthorized\r\n"
            "CSeq: %d\r\n"
            "WWW-Authenticate: Basic realm=\"Access the camera streams\"\r\n"
            "\r\n", p->cseq);
}

static void __method_options(struct connection_item_t *p, rtsp_handle h)
{
    __rtsp_write(p, "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE\r\n"
            "\r\n", p->cseq);
}

static void __method_describe(struct connection_item_t *p, rtsp_handle h)
{
    char sdp[__RTSP_TCP_BUF_SIZE];

    const char baseRtp[] =
        "v=0\r\n"
        "o=- 0 0 IN IP4 127.0.0.1\r\n"
        "s=librtsp\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "t=0 0\r\n"
        "a=range:npt=0-\r\n";
    char audioRtp[256] = "\r\n";
    char audioRtpfmt[16];

    if (h->audioPt != 255) {
        switch (h->audioPt) {
            case 0: strncpy(audioRtpfmt, "PCMU", 16 - 1); break;
            case 8: strncpy(audioRtpfmt, "PCMA", 16 - 1); break;
            case 14: strncpy(audioRtpfmt, "MPA", 16 - 1); break;
            case 18: strncpy(audioRtpfmt, "G729", 16 - 1); break;
            case 96: strncpy(audioRtpfmt, "MPEG4-GENERIC", 16 - 1); break;
            default: strncpy(audioRtpfmt, "UNKNOWN", 16 - 1); break;
        }
        sprintf(audioRtp,
            "\r\n"
            "m=audio 0 RTP/AVP %d\r\n"
            "a=control:track=1\r\n"
            "a=rtpmap:%d %s/%d\r\n",
            h->audioPt, h->audioPt, audioRtpfmt, (h->audioPt == 8 || h->audioPt == 0) ? 8000 : 90000);
    }

    if (h->isH265 &&
        h->sprop_vps_b64 && h->sprop_sps_b64 && h->sprop_sps_b16 && h->sprop_pps_b64) {
        DASSERT(h->sprop_vps_b64->result, return);
        DASSERT(h->sprop_sps_b64->result, return);
        DASSERT(h->sprop_sps_b16->result, return);
        DASSERT(h->sprop_pps_b64->result, return);

        DBG("VPS BASE64:%s\n", h->sprop_vps_b64->result);
        DBG("SPS BASE64:%s\n", h->sprop_sps_b64->result);
        DBG("SPS BASE16:%s\n", h->sprop_sps_b16->result);
        DBG("PPS BASE64:%s\n", h->sprop_pps_b64->result);

        snprintf(sdp, __RTSP_TCP_BUF_SIZE- 1,
                "%sm=video 0 RTP/AVP 96\r\n"
                "a=control:track=0\r\n"
                "a=rtpmap:96 H265/90000\r\n"
                "a=fmtp:96 profile-level-id=%s;"
                " packetization-mode=1;"
                " sprop-parameter-sets=%s,%s,%s;%s",
                baseRtp,
                h->sprop_sps_b16->result,
                h->sprop_vps_b64->result,
                h->sprop_sps_b64->result,
                h->sprop_pps_b64->result,
                audioRtp);
    } else if (!h->isH265 &&
        h->sprop_sps_b64 && h->sprop_sps_b16 && h->sprop_pps_b64) {
        DASSERT(h->sprop_sps_b64->result, return);
        DASSERT(h->sprop_sps_b16->result, return);
        DASSERT(h->sprop_pps_b64->result, return);

        DBG("SPS BASE64:%s\n", h->sprop_sps_b64->result);
        DBG("SPS BASE16:%s\n", h->sprop_sps_b16->result);
        DBG("PPS BASE64:%s\n", h->sprop_pps_b64->result);

        snprintf(sdp, __RTSP_TCP_BUF_SIZE - 1,
                "%sm=video 0 RTP/AVP 96\r\n"
                "a=control:track=0\r\n"
                "a=rtpmap:96 H264/90000\r\n"
                "a=fmtp:96 profile-level-id=%s;"
                " packetization-mode=1;"
                " sprop-parameter-sets=%s,%s;%s",
                baseRtp,
                h->sprop_sps_b16->result,
                h->sprop_sps_b64->result,
                h->sprop_pps_b64->result,
                audioRtp);
    } else {
        snprintf(sdp, __RTSP_TCP_BUF_SIZE - 1,
                "%sm=video 0 RTP/AVP 96\r\n"
                "a=control:track=0\r\n"
                "a=rtpmap:96 %s/90000\r\n"
                "a=fmtp:96 packetization-mode=1;%s",
                baseRtp,
                h->isH265 ? "H265" : "H264",
                audioRtp);
    }

    __rtsp_write(p, "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Content-Type: application/sdp\r\n"
            "Content-Length: %d\r\n"
            "\r\n"
            "%s", p->cseq, strlen(sdp), sdp);
}

static void __method_setup(struct connection_item_t *p, rtsp_handle h)
{
    /* make randomized session id */
	if (!p->session_id) {
    	p->session_id = __get_random_llu(&h->ctx);
    	p->ssrc = (unsigned int)(__get_random_llu(&h->ctx));
	}

    DBG("created session id %llx\n", p->session_id);

    if (p->trans[p->track_id].is_tcp) {
        __rtsp_write(p, "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Transport: RTP/AVP/TCP;unicast;interleaved=%u-%u\r\n"
            "Session: %llx\r\n"
            "\r\n", p->cseq,
            p->trans[p->track_id].channel_rtp,
            p->trans[p->track_id].channel_rtcp,
            p->session_id);
    } else {
        p->trans[p->track_id].server_port_rtp = SERVER_RTP_PORT + p->track_id;
        p->trans[p->track_id].server_port_rtcp = SERVER_RTCP_PORT + p->track_id;

        __rtsp_write(p, "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Transport: RTP/AVP/UDP;unicast;client_port=%u-%u;server_port=%u-%u\r\n"
            "Session: %llx\r\n"
            "\r\n", p->cseq,
            p->trans[p->track_id].client_port_rtp,
            p->trans[p->track_id].client_port_rtcp,
            p->trans[p->track_id].server_port_rtp,
            p->trans[p->track_id].server_port_rtcp,
            p->session_id);
    }

    p->con_state = __CON_S_READY;
}

static void __method_pause(struct connection_item_t *p, rtsp_handle h)
{
    __rtsp_write(p,
        "RTSP/1.0 "__RESPONCE_STR_METHODNOTALLOWED "\r\n");
}

static void __method_record(struct connection_item_t *p, rtsp_handle h)
{
    __rtsp_write(p,
        "RTSP/1.0 " __RESPONCE_STR_METHODNOTALLOWED "\r\n");
}

static void __method_error(struct connection_item_t *p, rtsp_handle h)
{
    __rtsp_write(p,
        "RTSP/1.0 " __RESPONCE_STR_SERVERERROR "\r\n");
}

static void __method_play(struct connection_item_t *p, rtsp_handle h)
{
    __rtsp_write(p,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Range: npt=now-\r\n"
        "\r\n" , p->cseq);

    for (int i = 0; i < sizeof(p->trans) / sizeof(*p->trans); i++) {
        if (!p->trans[i].server_port_rtp) continue;
        p->track_id = i;

        ASSERT(__bind_rtcp(p) == SUCCESS, return);
        ASSERT(__bind_rtp(p) == SUCCESS, return);
        p->trans[p->track_id].rtp_timestamp = (millis() * 90) & UINT32_MAX;
        p->trans[p->track_id].rtp_seq = rand_r(&h->ctx);
        p->trans[p->track_id].rtcp_octet = 0;
        p->trans[p->track_id].rtcp_packet_cnt = 0;
        p->trans[p->track_id].rtcp_tick_org = 150; // TODO: must be variant
        p->trans[p->track_id].rtcp_tick = p->trans[p->track_id].rtcp_tick_org;

        ASSERT(__rtcp_send_sr(p, p->track_id) == SUCCESS, return);
    }

    p->con_state = __CON_S_PLAYING;

    request_idr();
}

static int __method_teardown(struct connection_item_t *p, rtsp_handle h)
{
    __rtsp_write(p,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "\r\n", p->cseq);

    p->con_state = __CON_S_INIT;

    return SUCCESS;
}


/******************************************************************************
 *              METHOD IMPLEMENTATIONS
 ******************************************************************************/

static int __message_proc_sock(struct list_t *e, void *p)
{
    struct connection_item_t *con;
    struct sock_select_t *socks = p;
    char buf[__RTSP_TCP_BUF_SIZE];
    rtsp_handle h = NULL;

    DASSERT(socks, return FAILURE);
    MUST(h = socks->h_rtsp, return FAILURE);

    list_upcast(con, e);

    if (con->con_state == __CON_S_DISCONNECTED) {
        ERR("zombie connection detected: report to author\n");
        return SUCCESS;
    }

    if (FD_ISSET(con->client_fd, &(socks->rfds))) {
        int first_char = fgetc(con->fp_tcp_read);
        if (first_char == '$') {
            unsigned char head[3];
            if (fread(head, 1, 3, con->fp_tcp_read) == 3) {
                int len = (head[1] << 8) | head[2];
                while (len > 0) {
                    int r = fread(buf, 1, min(len, sizeof(buf)), con->fp_tcp_read);
                    if (r <= 0) break;
                    len -= r;
                }
                DBG("discarded interleaved packet (%d bytes)\n", (head[1] << 8) | head[2]);
            }
            return SUCCESS;
        } else if (first_char != EOF) {
            ungetc(first_char, con->fp_tcp_read);
        } else {
            con->con_state = __CON_S_DISCONNECTED;
            ASSERT(bufpool_detach(con->pool, con) == SUCCESS, ERR("connection detach failed\n"));
            return SUCCESS;
        }

        con->parser_state = __PARSER_S_INIT;
        con->method = __METHOD_NONE;

        char header = 0, isAuthValid = 0, *tok, *last;
        unsigned long long session_id;
        /* parse line by line. hereafter parser is switched according to the finite state machine */
        while (__read_line(con, buf)) {
            if (header < 1) {
                con->track_id = 0;
                if (SCMP(__STR_OPTIONS, buf))            { con->method = __METHOD_OPTIONS;
                } else if (SCMP(__STR_DESCRIBE, buf))    { con->method = __METHOD_DESCRIBE;
                } else if (SCMP(__STR_SETUP, buf))       { con->method = __METHOD_SETUP;
                    STR_KEY_NUM(buf, "track=", con->track_id);
                } else if (SCMP(__STR_PLAY, buf))        { con->method = __METHOD_PLAY;
                } else if (SCMP(__STR_RECORDING, buf))   { con->method = __METHOD_RECORDING;
                } else if (SCMP(__STR_PAUSE, buf))       { con->method = __METHOD_PAUSE;
                } else if (SCMP(__STR_TEARDOWN, buf))    { con->method = __METHOD_TEARDOWN;
                } header++;
            }

            if (SCMP(__STR_AUTH, buf) && h->isAuthOn) {
                    char cred[66], valid[256];
                    sprintf(cred, "%s:%s", h->user, h->pass);
                    strcpy(valid, "Basic ");
                    base64_encode(valid + 6, cred, strlen(cred));
                    const char *received = buf + strlen(__STR_AUTH) + 2;
                    size_t length = strcspn(received, "\r\n");
                    isAuthValid = length == strlen(valid) && !memcmp(received, valid, length) &&
                        (!received[length] || !strcmp(received + length, "\r\n"));
             } else if (SCMP(__STR_CSEQ, buf)) {
                ASSERT(tok = strtok_r(buf, ": ", &last), goto error);
                ASSERT(tok = strtok_r(NULL, ": ", &last), goto error);
                ASSERT((con->cseq = atoi(tok)) > 0, goto error);
                con->parser_state = __PARSER_S_CSEQ;
            } else if (SCMP(__STR_RANGE, buf)) {
                con->parser_state = __PARSER_S_RANGE;
            } else if (SCMP(__STR_SESSION, buf)) {
                ASSERT(tok = strtok_r(buf, ": ", &last), goto error);
                ASSERT(tok = strtok_r(NULL, ": ", &last), goto error);
                ASSERT(sscanf(tok, "%llx", &session_id) > 0, goto error);
                con->given_session_id = session_id;
                con->parser_state = __PARSER_S_SESSION;
            } else if (SCMP(__STR_TRANSPORT, buf)) {
                if (strstr(buf, __STR_RTP_AVP_TCP)) {
                    con->trans[con->track_id].is_tcp = 1;
                }
                for (tok = strtok_r(buf, "; ", &last); tok != NULL; tok = strtok_r(NULL, "; ", &last)) {
                    if (SCMP(__STR_CLIENTPORT, tok)) {
                        ASSERT(sscanf(tok, __STR_CLIENTPORT "=%u-%u",
                            &con->trans[con->track_id].client_port_rtp,
                            &con->trans[con->track_id].client_port_rtcp) > 0,
                                goto error);

                        con->parser_state = __PARSER_S_TRANSPORT;
                    } else if (SCMP(__STR_INTERLEAVED, tok)) {
                        ASSERT(sscanf(tok, __STR_INTERLEAVED "=%hhu-%hhu",
                            &con->trans[con->track_id].channel_rtp,
                            &con->trans[con->track_id].channel_rtcp) > 0,
                                goto error);

                        con->parser_state = __PARSER_S_TRANSPORT;
                    }
                }
            }
            continue;
error:
            __PARSE_ERROR(con);
        }

        if (con->parser_state == __PARSER_S_ERROR) {
            __method_error(con, h);
        } else {
            if (con->method != __METHOD_NONE && h->isAuthOn && !isAuthValid)
                con->method = __METHOD_AUTH;
            switch (con->method) {
                case __METHOD_AUTH: __method_auth(con, h); break;
                case __METHOD_OPTIONS: __method_options(con, h); break;
                case __METHOD_DESCRIBE: __method_describe(con, h); break;
                case __METHOD_SETUP: __method_setup(con, h); break;
                case __METHOD_PLAY: __method_play(con, h); break;
                case __METHOD_PAUSE: __method_pause(con, h); break;
                case __METHOD_RECORDING: __method_record(con, h); break;
                case __METHOD_TEARDOWN: __method_teardown(con, h); break;
                case __METHOD_NONE:
                    /* state DISCONNECTED connections should be garbage collected immediately.
                       but sending thread might watches the connection right now.
                       so the connection might live at here */
                    if (con->con_state != __CON_S_DISCONNECTED) {
                        ERR("unexpected empty request, forcing disconnect\n");
                        con->con_state = __CON_S_DISCONNECTED;
                    }
                    ASSERT(bufpool_detach(con->pool, con) == SUCCESS, ERR("connection detach failed\n"));
                    break;
                default: ERR("unexpected method state\n"); return FAILURE;
            }
        }
    }
    return SUCCESS;
}


/******************************************************************************
 *              NETWORK FUNCTIONS
 ******************************************************************************/

static int __connection_reset(void *v)
{
    struct connection_item_t *p = v;
    unsigned ctx;

    if (p->con_state != __CON_S_DISCONNECTED) {
        DBG("force connection to close\n");
    }

    FCLOSE(p->fp_tcp_read);
    FCLOSE(p->fp_tcp_write);
    CLOSE(p->client_fd);

    p->client_fd = 0;
    p->con_state = __CON_S_DISCONNECTED;

    for (int i = 0; i < sizeof(p->trans) / sizeof(*p->trans); i++) {
        if (p->trans[i].server_rtcp_fd != 0) {
            CLOSE(p->trans[i].server_rtcp_fd);
            p->trans[i].server_rtcp_fd = 0;
        }

        if (p->trans[i].server_rtp_fd != 0) {
            CLOSE(p->trans[i].server_rtp_fd);
            p->trans[i].server_rtp_fd = 0;
        }
    }

    p->given_session_id = 0;
    p->cseq = 0;

    ctx = p->trans[0].rtp_timestamp;

    /* randomize session id to avoid conflict */
    p->session_id = __get_random_llu(&ctx);

    return SUCCESS;
}

    static inline int
__connection_list_add(bufpool_handle con_pool, struct list_head_t *head, int fd, struct sockaddr_in addr)
{
    DASSERT(head, return FAILURE);
    DASSERT(fd > 0, return FAILURE);

    struct connection_item_t *p = NULL;

    ASSERT(bufpool_get_free(con_pool, &p) == SUCCESS, return FAILURE);

    DASSERT(p, return FAILURE);

    DBG("previous fd=%d\n", p->client_fd);

    p->addr=addr;
    p->client_fd=fd;

    ASSERT((p->fp_tcp_read = fdopen(fd, "r")), goto error);
    ASSERT((p->fp_tcp_write = fdopen(fd, "w")), goto error);

    p->tx_len = 0;
    if (!p->tx_buf) p->tx_buf = malloc(RTSP_TX_BATCH);
    p->con_state = __CON_S_INIT;

    return list_add(head, &(p->list_entry));
error:
    __connection_reset(&p->list_entry);
    return FAILURE;
}

static inline int __find_fd_max(struct list_head_t *head)
{
    struct list_t *p;
    struct connection_item_t *c;
    int m = -1;
    p = head->list;
    while (p) {
        list_upcast(c,p);
        if (c->con_state != __CON_S_DISCONNECTED) {
            m = max(c->client_fd, m);
        }
        p = p->next;
    }
    return m;
}

static inline int __set_select_sock(struct list_t *p, void *param)
{
    struct connection_item_t *c;
    struct sock_select_t *socks = param;

    list_upcast(c, p);

    FD_SET(c->client_fd, &(socks->rfds));

    return SUCCESS;
}

static inline int __bind_tcp(unsigned short port)
{
    int server_fd = 0;
    struct sockaddr_in addr;
    int tmp = 1;

    /* setup serve rsocket */
    ASSERT((server_fd = socket(AF_INET, SOCK_STREAM, 0)) > 0, ({
                ERR("socket:%s\n", strerror(errno));
                goto error;}));

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(tmp));

    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_family = AF_INET;

    ASSERT(bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0, ({
                ERR("bind:%s\n", strerror(errno));
                goto error;}));

    ASSERT(listen(server_fd, 5) >= 0, ({
                ERR("listen:%s\n", strerror(errno));
                goto error;}));

    /* set the socket to non-blocking */
    fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL) | O_NONBLOCK);

    return server_fd;
error:
    if (server_fd > 0) close(server_fd);
    return FAILURE;
}

static inline int __bind_rtp(struct connection_item_t *con )
{
    int server_fd = -1;
    struct sockaddr_in addr = {};
    int tmp;

    /* reset socket */
    if (con->trans[con->track_id].server_rtp_fd != 0) {
        CLOSE(con->trans[con->track_id].server_rtp_fd);
        con->trans[con->track_id].server_rtp_fd = 0;
    }
    /* setup serve rsocket */
    ASSERT((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) > 0, ({
                ERR("socket:%s\n", strerror(errno));
                goto error;}));

    addr.sin_port = htons(con->trans[con->track_id].server_port_rtp);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_family = AF_INET;

    tmp = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(tmp));
    {
        int sz = 512 * 1024;   /* absorb a whole keyframe, see the TCP accept path */
        if (setsockopt(server_fd, SOL_SOCKET, SO_SNDBUFFORCE, &sz, sizeof(sz)))
            setsockopt(server_fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
    }

    ASSERT(bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0, ({
                ERR("bind:%s\n", strerror(errno));
                goto error;}));

    addr = con->addr;
    addr.sin_port=htons(con->trans[con->track_id].client_port_rtp);

    ASSERT(connect(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0, ({
                ERR("connect:%s\n", strerror(errno));
                goto error;}));

    /* set the socket to non-blocking */
    tmp = 1;
    ASSERT(ioctl(server_fd, FIONBIO, &tmp) != -1, ({
                ERR("ioctl:%s\n", strerror(errno));
                goto error;}));

    con->trans[con->track_id].server_rtp_fd = server_fd;

    return SUCCESS;
error:
    if (server_fd > 0) close(server_fd);
    return FAILURE;
}

static inline int __bind_rtcp(struct connection_item_t *con )
{
    int server_fd = -1;
    struct sockaddr_in addr = {};
    int tmp;

    /* reset socket */
    if (con->trans[con->track_id].server_rtcp_fd != 0) {
        CLOSE(con->trans[con->track_id].server_rtcp_fd);
        con->trans[con->track_id].server_rtcp_fd = 0;
    }

    /* setup serve rsocket */
    ASSERT((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) > 0, ({
                ERR("socket:%s\n", strerror(errno));
                goto error;}));

    addr.sin_port = htons(con->trans[con->track_id].server_port_rtcp);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_family = AF_INET;

    tmp = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(tmp));

    ASSERT(bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0, ({
                ERR("bind:%s\n", strerror(errno));
                goto error;}));

    addr = con->addr;
    addr.sin_port = htons(con->trans[con->track_id].client_port_rtcp);

    ASSERT(connect(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0, ({
                ERR("connect:%s\n", strerror(errno));
                goto error;}));

    con->trans[con->track_id].server_rtcp_fd = server_fd;

    return SUCCESS;
error:
    if (server_fd > 0) close(server_fd);
    return FAILURE;
}

static inline int __accept_proc_sock(rtsp_handle h, int server_fd, struct sock_select_t *p_socks)
{
    unsigned int len;
    int fd;
    struct sockaddr_in from_addr;

    if (FD_ISSET(server_fd, &p_socks->rfds) != 0) {
        /* accept new connection */
        len = sizeof(from_addr);

        fd = accept(server_fd, (struct sockaddr *)&from_addr,
                &len);

        /* we have selected this fd, but still EAGAIN may occur */
        if (fd < 0){
            ASSERT(errno == EAGAIN, ({
                        ERR("accept:%s\n",strerror(errno));
                        return FAILURE;}));
            return SUCCESS;
        }

        /* set server fd to non-blocking */
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

        /* A keyframe is a few hundred KB and the kernel's default TCP send
         * buffer (64 KB on small boards) cannot hold it: the interleaved sender
         * then spins on partial writes for as long as the link takes to drain,
         * hundreds of ms of CPU per keyframe. Ask for a buffer that fits one
         * (SO_SNDBUFFORCE bypasses wmem_max, which needs root). */
        {
            int sz = 512 * 1024;
            if (setsockopt(fd, SOL_SOCKET, SO_SNDBUFFORCE, &sz, sizeof(sz)))
                setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
        }

        /* update connection-list exclusively */
        ASSERT(__connection_list_add(h->con_pool, &h->con_list, fd, from_addr) == SUCCESS,
                return FAILURE);
    }

    return SUCCESS;
}

static int __connection_is_dead(struct list_t *l)
{
    struct connection_item_t *c;

    list_upcast(c, l);

    return c->con_state == __CON_S_DISCONNECTED;
}

/******************************************************************************
 *                  THREAD CALLBACKS
 ******************************************************************************/
static void *rtspThrFxn(void *v)
{
    thread_handle           h = v;
    rtsp_handle             rh = h->sharedp->param_shared;
    void                    *status = THREAD_FAILURE;
    struct sock_select_t    socks = {};

    int     ret_select;
    int     server_fd = -1;

    DASSERT(thread_check_isoleted_job(h) == SUCCESS, goto error);

    /* open tcp connection */
    ASSERT((server_fd = __bind_tcp(rh->port)) > 0, goto error);

    socks.nfds = server_fd + 1;
    socks.h_rtsp = rh;

    thread_sync_init(h);

    while (!gbl_get_quit(h->sharedp->gbl)) {
        FD_ZERO(&(socks.rfds));
        FD_SET(server_fd, &(socks.rfds));
        socks.timeout.tv_sec = 1;
        socks.timeout.tv_usec = 0;

        ASSERT(list_map_inline(&rh->con_list, (__set_select_sock), &socks) == SUCCESS, goto error);

        ASSERT((ret_select = select(socks.nfds, &(socks.rfds), NULL, NULL, &(socks.timeout))) >= 0, ({
                    ERR("select:%s\n", strerror(errno));
                    goto error;}));

        if (ret_select > 0){
            /* lock while tcp layer is done */
            rtsp_lock(rh);

            ASSERT(__accept_proc_sock(rh, server_fd, &socks) == SUCCESS,
                    ({ rtsp_unlock(rh); goto error;}));

            ASSERT(list_map_inline(&rh->con_list, __message_proc_sock, &socks) == SUCCESS,
                    ({ rtsp_unlock(rh); goto error;}));

            MUST(list_sweep(&rh->con_list, __connection_is_dead) == SUCCESS,
                    ({ rtsp_unlock(rh); goto error;}));

            socks.nfds = max(server_fd, __find_fd_max(&rh->con_list)) + 1;

            rtsp_unlock(rh);
        }
        //bufpool_statistics(rh->con_pool);
    }

    status = THREAD_SUCCESS;
error:
    /* Make sure the other threads aren't waiting for us */
    thread_sync_cleanup(h);

    if (server_fd > 0) close(server_fd);

    return status;
}


/******************************************************************************
 *              PUBLIC FUNCTIONS
 ******************************************************************************/
void rtsp_finish(rtsp_handle h)
{
    /* close every connections in the handle */
    if (h) {
        list_destroy(&h->con_list);

        if (h->pool) {
            gbl_set_quit(h->pool->sharedp->gbl);

            ASSERT(threadpool_join(h->pool) == SUCCESS, ERR("thread join with error\n"));

            bufpool_delete(h->con_pool);
            bufpool_delete(h->transfer_pool);

            mime_encoded_delete(h->sprop_vps_b64);
            mime_encoded_delete(h->sprop_sps_b64);
            mime_encoded_delete(h->sprop_sps_b16);
            mime_encoded_delete(h->sprop_pps_b64);

            threadpool_delete(h->pool);
        }

        pthread_mutex_destroy(&h->mutex);

        FREE(h);
    }

    return;
}

rtsp_handle rtsp_create(unsigned char max_con, unsigned int port, int priority)
{
    rtsp_handle       nh = NULL;

    ASSERT(max_con <= RTSP_MAXIMUM_CONNECTIONS,
            ({ERR("maximum number of connections should be within %d\n", RTSP_MAXIMUM_CONNECTIONS);
             return NULL;}));

    TALLOC(nh, return NULL);

    nh->audioPt = 255;
    nh->max_con = max_con;
    nh->port = port;
    nh->priority = priority;

    pthread_mutex_init(&nh->mutex,NULL);

    ASSERT(nh->pool = threadpool_create(nh), goto error);
    ASSERT(nh->con_pool =  __connectionpool_create(max_con), goto error);
    ASSERT(nh->transfer_pool =  __transpool_create(max_con), goto error);

    /* create tcp thread */
    ASSERT(CREATE_THREAD(nh->pool, rtspThrFxn, priority--, NULL),
            goto error);

    ASSERT(threadpool_start(nh->pool) == SUCCESS,
            goto error);

    srand((unsigned) time(NULL));

    return nh;

error:
    rtsp_finish(nh);
    return NULL;
}

void rtsp_configure_auth(rtsp_handle h, const char *user, const char *pass)
{
    if (user && pass) {
        h->isAuthOn = 1;
        strncpy(h->user, user, sizeof(h->user) - 1);
        strncpy(h->pass, pass, sizeof(h->pass) - 1);
    } else {
        h->isAuthOn = 0;
    }
}

int rtsp_tick(rtsp_handle h)
{
    ASSERT(h, return FAILURE);
    struct timespec ts;
    struct timeval tv;

    ASSERT(clock_gettime(CLOCK_MONOTONIC_COARSE, &ts) == 0, ({
        ERR("clock_gettime failed\n");
        return FAILURE;}));

    tv.tv_sec = ts.tv_sec;
    tv.tv_usec = ts.tv_nsec / 1000;

    return __get_timestamp_offset(&h->stat, &tv);
}

#else /* DIVINUS_OWNED_INPUT: owned per-handle server, no legacy thread pools. */
#include <poll.h>
#include <limits.h>
#include <stdint.h>

/* One-second total deadline per response or aggregate, with stop sampled at
 * most every 50 ms while a peer cannot accept more bytes. No stdio buffering. */
int64_t rtsp_owned_now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return -1;
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int rtsp_owned_stopping(rtsp_handle h)
{
    return atomic_load_explicit(&h->stopping, memory_order_relaxed) ||
        (h->owned_options.should_stop && h->owned_options.should_stop(h->owned_options.context));
}

int rtsp_owned_write(rtsp_handle h, int fd, const void *buf, size_t len, int64_t deadline)
{
    const unsigned char *p = buf;
    while (len) {
        if (rtsp_owned_stopping(h)) return -ECANCELED;
        int64_t now = rtsp_owned_now_ms();
        if (now < 0) return -EIO;
        if (now >= deadline) return -ETIMEDOUT;
        int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        ssize_t n = send(fd, p, len, flags);
        if (n > 0) { p += n; len -= (size_t)n; continue; }
        if (n == 0) return -EPIPE;
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -errno;
        struct pollfd f = { .fd = fd, .events = POLLOUT };
        int wait = (int)min(deadline - now, 50);
        int rc = poll(&f, 1, wait);
        if (rc < 0 && errno != EINTR) return -errno;
        if (rc > 0 && (f.revents & (POLLERR | POLLHUP | POLLNVAL))) return -EPIPE;
    }
    return 0;
}

static int owned_fd_flags(int fd, int socket_fd)
{
    int f = fcntl(fd, F_GETFL);
    if (f < 0 || fcntl(fd, F_SETFL, f | O_NONBLOCK) < 0 ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) return -errno;
#ifdef SO_NOSIGPIPE
    if (socket_fd) {
        int one = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one))) return -errno;
    }
#else
    (void)socket_fd;
#endif
    return 0;
}

/* Only the server thread or capture worker (under h->mutex) touch clients.
 * All arrays and buffers belong to this handle, never a process-global pool. */
void rtsp_owned_drop(struct connection_item_t *c)
{
    if (c->client_fd >= 0) close(c->client_fd);
    c->client_fd = -1;
    c->con_state = __CON_S_DISCONNECTED;
    c->request_len = c->interleaved_left = 0;
    c->request_since = 0;
    c->tx_len = c->tx_packets = c->tx_payload_octets = 0;
    c->session_id = 0;
    memset(c->trans, 0, sizeof(c->trans));
}

static int owned_response(struct connection_item_t *c, const char *status, const char *extra, const char *body)
{
    char reply[8192];
    int n;
    if (body)
        n = snprintf(reply, sizeof(reply), "RTSP/1.0 %s\r\nCSeq: %d\r\n%sContent-Length: %zu\r\n\r\n%s", status, c->cseq, extra, strlen(body), body);
    else
        n = snprintf(reply, sizeof(reply), "RTSP/1.0 %s\r\nCSeq: %d\r\n%s\r\n", status, c->cseq, extra);
    if (n < 0 || (size_t)n >= sizeof(reply)) return -EOVERFLOW;
    return rtsp_owned_write(c->owner, c->client_fd, reply, (size_t)n, c->response_deadline);
}

static int owned_decimal(const char *p, unsigned max_value, unsigned *v, const char **end)
{
    unsigned n = 0;
    if (*p < '0' || *p > '9') return -1;
    do {
        unsigned digit = (unsigned)(*p++ - '0');
        if (n > max_value / 10 || (n == max_value / 10 && digit > max_value % 10)) return -1;
        n = n * 10 + digit;
    } while (*p >= '0' && *p <= '9');
    *v = n; *end = p; return 0;
}

static int owned_session(const char *value, uint64_t *v)
{
    uint64_t n = 0; unsigned count = 0;
    while (*value) {
        unsigned d;
        char c = *value++;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        if (++count > 16) return -1;
        n = (n << 4) | d;
    }
    if (!count || !n) return -1;
    *v = n; return 0;
}

static int owned_transport(char *value, unsigned *a, unsigned *b)
{
    char *save = NULL, *field = strtok_r(value, ";", &save);
    if (!field || strcasecmp(field, "RTP/AVP/TCP")) return -1;
    int unicast = 0, channels = 0, mode = 0;
    while ((field = strtok_r(NULL, ";", &save))) {
        while (*field == ' ' || *field == '\t') ++field;
        size_t len = strlen(field);
        while (len && (field[len-1] == ' ' || field[len-1] == '\t')) field[--len] = 0;
        if (!strcasecmp(field, "unicast")) { if (unicast++) return -1; }
        else if (!strncasecmp(field, "interleaved=", 12)) {
            const char *end;
            if (channels++ || owned_decimal(field + 12, 255, a, &end) || *end++ != '-' ||
                owned_decimal(end, 255, b, &end) || *end || *a == 255 || *b != *a + 1) return -1;
        } else if (!strncasecmp(field, "mode=", 5)) {
            if (mode++ || (strcasecmp(field+5, "PLAY") && strcasecmp(field+5, "\"PLAY\""))) return -1;
        } else return -1;
    }
    return channels == 1 ? 0 : -1;
}

/* Validate option tags as tokens before echoing them in Unsupported. The
 * accumulated list is bounded by the same limit as the complete request. */
static int owned_option_tags(char *out, size_t capacity, const char *value)
{
    for (;;) {
        while (*value == ' ' || *value == '\t') ++value;
        const char *start = value;
        while ((*value >= 'a' && *value <= 'z') || (*value >= 'A' && *value <= 'Z') ||
               (*value >= '0' && *value <= '9') || (*value && strchr("!#$%&'*+-.^_`|~", *value))) ++value;
        size_t n = (size_t)(value - start), used = strlen(out);
        if (!n || used + n + (used ? 2 : 0) >= capacity) return -1;
        if (used) { out[used++] = ','; out[used++] = ' '; }
        memcpy(out + used, start, n); out[used+n] = 0;
        while (*value == ' ' || *value == '\t') ++value;
        if (!*value) return 0;
        if (*value++ != ',') return -1;
    }
}

static int owned_request(struct connection_item_t *c)
{
    rtsp_handle h = c->owner;
    char method[24], uri[1024], version[16]; int used = 0;
    char *line = c->request, *e = strstr(line, "\r\n");
    if (!e) return -EPROTO;
    *e = 0;
    c->cseq = 0;
    if (sscanf(line, "%23s %1023s %15s%n", method, uri, version, &used) != 3 ||
        line[used] || strcmp(version, "RTSP/1.0") || (strncmp(uri, "rtsp://", 7) && (strcmp(method, "OPTIONS") || strcmp(uri, "*"))))
        return owned_response(c, "400 Bad Request", "", NULL);
    char *auth = NULL, *transport = NULL, *session = NULL;
    int has_cseq = 0, content_length = 0, unsupported = 0, bad_range = 0;
    char unsupported_tags[__RTSP_TCP_BUF_SIZE] = {0};
    for (line = e + 2; *line; line = e + 2) {
        e = strstr(line, "\r\n");
        if (!e) return -EPROTO;
        if (e == line) break;
        *e = 0;
        char *colon = strchr(line, ':');
        if (!colon || colon == line) return -EPROTO;
        *colon++ = 0;
        while (*colon == ' ' || *colon == '\t') ++colon;
        const char *end; unsigned n;
        if (!strcasecmp(line, "Authorization")) {
            if (auth) return -EPROTO;
            auth = colon;
        } else if (!strcasecmp(line, "CSeq")) {
            if (has_cseq++ || owned_decimal(colon, INT_MAX, &n, &end) || *end || !n) return -EPROTO;
            c->cseq = (int)n;
        } else if (!strcasecmp(line, "Transport")) {
            if (transport) return -EPROTO;
            transport = colon;
        } else if (!strcasecmp(line, "Session")) {
            if (session) return -EPROTO;
            session = colon;
        } else if (!strcasecmp(line, "Require") || !strcasecmp(line, "Proxy-Require")) {
            if (owned_option_tags(unsupported_tags, sizeof(unsupported_tags), colon)) return -EPROTO;
        } else if (!strcasecmp(line, "Range")) {
            if (strcmp(colon, "npt=now-") && strcmp(colon, "npt=0.000-") && strcmp(colon, "npt=0-")) bad_range = 1;
        } else if (!strcasecmp(line, "Scale") || !strcasecmp(line, "Speed")) {
            if (strcmp(colon, "1") && strcmp(colon, "1.0")) unsupported = 1;
        } else if (!strcasecmp(line, "Content-Length")) {
            if (content_length++ || strcmp(colon, "0")) return -EPROTO;
        }
    }
    if (!has_cseq) return -EPROTO;
    /* Compare the complete canonical encoded value, not an accepted prefix.
     * No decoder that skips punctuation or whitespace is used. */
    if (!auth || strcmp(auth, h->authorization))
        return owned_response(c, "401 Unauthorized", "WWW-Authenticate: Basic realm=\"owned stream\"\r\n", NULL);
    c->last_request = rtsp_owned_now_ms();
    if (*unsupported_tags) {
        char extra[__RTSP_TCP_BUF_SIZE + 32];
        snprintf(extra, sizeof(extra), "Unsupported: %s\r\n", unsupported_tags);
        return owned_response(c, "551 Option Not Supported", extra, NULL);
    }
    if (unsupported) return owned_response(c, "406 Not Acceptable", "", NULL);
    if (bad_range) return owned_response(c, "457 Invalid Range", "", NULL);
    if (!strcmp(method, "OPTIONS"))
        return owned_response(c, "200 OK", "Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN, GET_PARAMETER\r\n", NULL);
    if (!strcmp(method, "DESCRIBE")) {
        char sdp[4096];
        int n = snprintf(sdp, sizeof(sdp), "v=0\r\no=- 0 0 IN IP4 %s\r\ns=owned H264\r\nc=IN IP4 0.0.0.0\r\nt=0 0\r\na=control:*\r\nm=video 0 RTP/AVP 96\r\na=control:track=0\r\na=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1;profile-level-id=%s;sprop-parameter-sets=%s,%s\r\n", h->bind_address, h->sprop_sps_b16->result, h->sprop_sps_b64->result, h->sprop_pps_b64->result);
        if (n < 0 || (size_t)n >= sizeof(sdp)) return -EOVERFLOW;
        char extra[1200];
        snprintf(extra, sizeof(extra), "Content-Type: application/sdp\r\nContent-Base: %s%s\r\n", uri, uri[strlen(uri)-1] == '/' ? "" : "/");
        return owned_response(c, "200 OK", extra, sdp);
    }
    if (!strcmp(method, "SETUP")) {
        const char *track = strrchr(uri, '/');
        if (!track || strcmp(track + 1, "track=0")) return owned_response(c, "404 Not Found", "", NULL);
        unsigned a, b;
        if (!transport || owned_transport(transport, &a, &b)) return owned_response(c, "461 Unsupported Transport", "", NULL);
        if (c->con_state != __CON_S_INIT) return owned_response(c, "455 Method Not Valid in This State", "", NULL);
        c->trans[0].is_tcp = 1;
        c->trans[0].channel_rtp = a; c->trans[0].channel_rtcp = b;
        c->trans[0].rtp_seq = (unsigned short)++h->session_serial;
        c->session_id = ++h->session_serial;
        c->ssrc = (unsigned int)h->session_serial;
        c->con_state = __CON_S_READY;
        c->last_sr_ms = rtsp_owned_now_ms();
        char extra[192];
        snprintf(extra, sizeof(extra), "Transport: RTP/AVP/TCP;unicast;interleaved=%u-%u\r\nSession: %llx;timeout=60\r\n", a, b, (unsigned long long)c->session_id);
        return owned_response(c, "200 OK", extra, NULL);
    }
    if (!strcmp(method, "PLAY") || !strcmp(method, "PAUSE") || !strcmp(method, "TEARDOWN") || !strcmp(method, "GET_PARAMETER")) {
        if (c->con_state != __CON_S_READY && c->con_state != __CON_S_PLAYING)
            return owned_response(c, "455 Method Not Valid in This State", "", NULL);
        uint64_t id;
        if (!session || owned_session(session, &id) || id != c->session_id)
            return owned_response(c, "454 Session Not Found", "", NULL);
        char extra[1200];
        if (!strcmp(method, "PLAY"))
            snprintf(extra, sizeof(extra), "Session: %llx\r\nRange: npt=now-\r\nRTP-Info: url=%s%strack=0;seq=%u\r\n", (unsigned long long)c->session_id, uri, uri[strlen(uri)-1]=='/' ? "" : "/", c->trans[0].rtp_seq);
        else snprintf(extra, sizeof(extra), "Session: %llx\r\n", (unsigned long long)c->session_id);
        int rc = owned_response(c, "200 OK", extra, NULL);
        if (rc) return rc;
        if (!strcmp(method, "PLAY")) c->con_state = __CON_S_PLAYING;
        if (!strcmp(method, "PAUSE")) c->con_state = __CON_S_READY;
        if (!strcmp(method, "TEARDOWN")) { c->con_state = __CON_S_INIT; c->session_id = 0; c->accepted_at = c->last_request; memset(c->trans, 0, sizeof(c->trans)); }
        return 0;
    }
    return owned_response(c, "405 Method Not Allowed", "", NULL);
}

/* Incremental input handles split headers and RTCP frames without blocking on
 * a peer. One bounded recv per poll iteration prevents a busy peer monopolizing
 * the server. Requests have no body; nonzero Content-Length closes the peer. */
static int owned_read(struct connection_item_t *c)
{
    unsigned char incoming[4096];
    int64_t now = rtsp_owned_now_ms();
    if (now < 0) return -EIO;
    /* One peer cannot renew the lock-holding write budget by pipelining. */
    c->response_deadline = now + 200;
    ssize_t n = recv(c->client_fd, incoming, sizeof(incoming), MSG_DONTWAIT);
    if (n == 0) return -EPIPE;
    if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -errno;
    for (ssize_t i = 0; i < n; ++i) {
        if (c->interleaved_left) { --c->interleaved_left; continue; }
        if (c->request_len == sizeof(c->request) - 1) return -EMSGSIZE;
        if (!c->request_len) c->request_since = rtsp_owned_now_ms();
        c->request[c->request_len++] = incoming[i];
        if (c->request[0] == '$') {
            if (c->request_len == 4) {
                if (!c->trans[0].is_tcp || (unsigned char)c->request[1] != c->trans[0].channel_rtcp) return -EPROTO;
                c->interleaved_left = ((unsigned char)c->request[2] << 8) | (unsigned char)c->request[3];
                c->request_len = 0;
                c->last_request = rtsp_owned_now_ms();
            }
        } else {
            if (!incoming[i] || (incoming[i] < 32 && incoming[i] != '\r' && incoming[i] != '\n' && incoming[i] != '\t')) return -EPROTO;
            if (c->request_len >= 4 && !memcmp(c->request + c->request_len - 4, "\r\n\r\n", 4)) {
                c->request[c->request_len] = 0;
                int rc = owned_request(c);
                c->request_len = 0; c->request_since = 0;
                if (rc) return rc;
                if (rtsp_owned_stopping(c->owner)) return -ECANCELED;
            }
        }
    }
    return 0;
}

static void *owned_server(void *arg)
{
    rtsp_handle h = arg;
    int failure = 0;
    while (!rtsp_owned_stopping(h)) {
        struct pollfd f[RTSP_MAXIMUM_CONNECTIONS + 2];
        f[0] = (struct pollfd){h->wake[0], POLLIN, 0};
        f[1] = (struct pollfd){h->listener, POLLIN, 0};
        rtsp_lock(h);
        for (unsigned i = 0; i < RTSP_MAXIMUM_CONNECTIONS; ++i)
            f[i+2] = (struct pollfd){h->connections[i].client_fd, POLLIN, 0};
        rtsp_unlock(h);
        int rc = poll(f, RTSP_MAXIMUM_CONNECTIONS + 2, 50);
        if (rc < 0) { if (errno == EINTR) continue; failure = -errno; break; }
        if (rtsp_owned_stopping(h)) break;
        if (f[0].revents || (f[1].revents & (POLLERR | POLLHUP | POLLNVAL))) { failure = -EIO; break; }
        rtsp_lock(h);
        int64_t now = rtsp_owned_now_ms();
        if (now < 0) failure = -EIO;
        for (unsigned i = 0; i < RTSP_MAXIMUM_CONNECTIONS; ++i) {
            struct connection_item_t *c = &h->connections[i];
            if (c->client_fd < 0 || c->client_fd != f[i+2].fd) continue;
            if ((f[i+2].revents & (POLLERR | POLLHUP | POLLNVAL)) ||
                ((f[i+2].revents & POLLIN) && owned_read(c)) ||
                ((c->request_len || c->interleaved_left) && now - c->request_since >= 5000) ||
                (c->con_state == __CON_S_INIT && now - c->accepted_at >= 5000) ||
                (c->con_state != __CON_S_INIT && now - c->last_request >= 60000)) rtsp_owned_drop(c);
            if (c->client_fd >= 0) {
                int rc = rtsp_owned_rtcp(c);
                if (rc == -EPIPE || rc == -ECONNRESET || rc == -ETIMEDOUT || rc == -ENOTCONN || rc == -ECONNABORTED) rtsp_owned_drop(c);
                else if (rc && rc != -ECANCELED) failure = rc;
            }
        }
        if (f[1].revents & POLLIN) {
            int fd = accept(h->listener, NULL, NULL);
            if (fd >= 0) {
                unsigned i;
                for (i = 0; i < RTSP_MAXIMUM_CONNECTIONS; ++i) if (h->connections[i].client_fd < 0) break;
                if (i == RTSP_MAXIMUM_CONNECTIONS || owned_fd_flags(fd, 1)) close(fd);
                else {
                    struct connection_item_t *c = &h->connections[i];
                    c->client_fd = fd; c->con_state = __CON_S_INIT; c->accepted_at = c->last_request = rtsp_owned_now_ms();
                }
            } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) failure = -errno;
        }
        rtsp_unlock(h);
        if (failure) break;
    }
    if (failure && !rtsp_owned_stopping(h)) {
        atomic_store_explicit(&h->stopping, 1, memory_order_relaxed);
        if (h->owned_options.failed) h->owned_options.failed(h->owned_options.context, failure);
    }
    return NULL;
}

static int owned_credential(const char *s, int user)
{
    if (!s || !*s || strnlen(s, 32) >= 32) return 0;
    for (; *s; ++s) if ((unsigned char)*s < 33 || (unsigned char)*s > 126 || (user && *s == ':')) return 0;
    return 1;
}

int rtsp_owned_prepare(const struct rtsp_owned_options *options, rtsp_handle *out)
{
    if (!out) return -EINVAL;
    *out = NULL;
    struct in_addr address;
    if (!options || !options->bind_address || strnlen(options->bind_address, INET_ADDRSTRLEN) >= INET_ADDRSTRLEN ||
        inet_pton(AF_INET, options->bind_address, &address) != 1 ||
        !owned_credential(options->user, 1) || !owned_credential(options->pass, 0)) return -EINVAL;
    rtsp_handle h = calloc(1, sizeof(*h));
    if (!h) return -ENOMEM;
    h->listener = h->wake[0] = h->wake[1] = -1;
    int rc = pthread_mutex_init(&h->mutex, NULL);
    if (rc) { free(h); return -rc; }
    atomic_init(&h->stopping, 0);
    h->owned_options = *options;
    strcpy(h->bind_address, options->bind_address);
    strcpy(h->user, options->user); strcpy(h->pass, options->pass);
    h->owned_options.bind_address = h->bind_address;
    h->owned_options.user = h->user; h->owned_options.pass = h->pass;
    h->isAuthOn = 1; h->audioPt = 255; h->port = options->port;
    h->connections = calloc(RTSP_MAXIMUM_CONNECTIONS, sizeof(*h->connections));
    if (!h->connections) { rc = -ENOMEM; goto error; }
    /* Initialize every fd before any fallible per-client allocation. */
    for (unsigned i = 0; i < RTSP_MAXIMUM_CONNECTIONS; ++i) h->connections[i].client_fd = -1;
    for (unsigned i = 0; i < RTSP_MAXIMUM_CONNECTIONS; ++i) {
        h->connections[i].owner = h;
        h->connections[i].con_state = __CON_S_DISCONNECTED;
        h->connections[i].tx_buf = malloc(RTSP_TX_BATCH);
        if (!h->connections[i].tx_buf) { rc = -ENOMEM; goto error; }
    }
    char credentials[64];
    snprintf(credentials, sizeof(credentials), "%s:%s", h->user, h->pass);
    mime_encoded_handle auth = mime_base64_create(credentials, strlen(credentials));
    memset(credentials, 0, sizeof(credentials));
    if (!auth) { rc = -ENOMEM; goto error; }
    snprintf(h->authorization, sizeof(h->authorization), "Basic %s", auth->result);
    mime_encoded_delete(auth);
    *out = h;
    return 0;
error:
    rtsp_owned_finish(h);
    return rc;
}

int rtsp_owned_start(rtsp_handle h, unsigned short *bound_port)
{
    if (!h || !bound_port) return -EINVAL;
    *bound_port = 0;
    if (h->started || h->thread_created) return -EALREADY;
    if (rtsp_owned_stopping(h)) return -ECANCELED;
    if (h->isH265 || !h->sprop_sps_b64 || !h->sprop_sps_b16 || !h->sprop_pps_b64) return -ENODATA;
    /* Keep SDP bounded before listener acceptance, including base64 expansion. */
    if (strlen(h->sprop_sps_b64->result) + strlen(h->sprop_pps_b64->result) > 3000) return -EMSGSIZE;
    int rc;
    if (pipe(h->wake)) return -errno;
    if ((rc = owned_fd_flags(h->wake[0], 0)) || (rc = owned_fd_flags(h->wake[1], 0))) goto error;
    h->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (h->listener < 0) { rc = -errno; goto error; }
    if ((rc = owned_fd_flags(h->listener, 1))) goto error;
    int one = 1;
    if (setsockopt(h->listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one))) { rc = -errno; goto error; }
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_port = htons(h->port) };
    if (inet_pton(AF_INET, h->bind_address, &address.sin_addr) != 1) { rc = -EINVAL; goto error; }
    if (bind(h->listener, (struct sockaddr *)&address, sizeof(address)) || listen(h->listener, 16)) { rc = -errno; goto error; }
    socklen_t size = sizeof(address);
    if (getsockname(h->listener, (struct sockaddr *)&address, &size)) { rc = -errno; goto error; }
    h->started = 1;
    rc = pthread_create(&h->worker, NULL, owned_server, h);
    if (rc) { rc = -rc; h->started = 0; goto error; }
    h->thread_created = 1;
    *bound_port = ntohs(address.sin_port);
    return 0;
error:
    if (h->listener >= 0) close(h->listener);
    if (h->wake[0] >= 0) close(h->wake[0]);
    if (h->wake[1] >= 0) close(h->wake[1]);
    h->listener = h->wake[0] = h->wake[1] = -1;
    return rc;
}

int rtsp_owned_finish(rtsp_handle h)
{
    if (!h) return 0;
    atomic_store_explicit(&h->stopping, 1, memory_order_relaxed);
    if (h->wake[1] >= 0) { char byte = 1; ssize_t ignored = write(h->wake[1], &byte, 1); (void)ignored; }
    if (h->thread_created) {
        int rc = pthread_join(h->worker, NULL);
        /* Never destroy resources that a worker might still access. */
        if (rc) return -rc;
    }
    if (h->listener >= 0) close(h->listener);
    if (h->wake[0] >= 0) close(h->wake[0]);
    if (h->wake[1] >= 0) close(h->wake[1]);
    if (h->connections) {
        for (unsigned i = 0; i < RTSP_MAXIMUM_CONNECTIONS; ++i) {
            rtsp_owned_drop(&h->connections[i]); free(h->connections[i].tx_buf);
        }
        free(h->connections);
    }
    mime_encoded_delete(h->sprop_vps_b64); mime_encoded_delete(h->sprop_sps_b64);
    mime_encoded_delete(h->sprop_sps_b16); mime_encoded_delete(h->sprop_pps_b64);
    pthread_mutex_destroy(&h->mutex);
    memset(h->pass, 0, sizeof(h->pass)); memset(h->authorization, 0, sizeof(h->authorization));
    free(h);
    return 0;
}
#endif /* DIVINUS_OWNED_INPUT */
