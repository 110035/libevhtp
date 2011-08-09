#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <evhtp.h>

#ifndef DISABLE_EVTHR
int                use_threads = 0;
int                num_threads = 0;
#endif
char             * bind_addr   = "0.0.0.0";
uint16_t           bind_port   = 8081;
char             * ssl_pem     = NULL;
char             * ssl_ca      = NULL;
__thread event_t * timer_ev    = NULL;
__thread int       pause_count = 0;

struct pauser {
    event_t         * timer_ev;
    evhtp_request_t * request;
};

/* pause testing */
static void
resume_request_timer(int sock, short which, void * arg) {
    struct pauser * pause = arg;

    printf("resume_request_timer()\n");

    evtimer_del(pause->timer_ev);
    evhtp_request_resume(pause->request);
}

static evhtp_res
pause_cb(evhtp_request_t * request, evhtp_header_t * header, void * arg) {
    struct pauser * pause = arg;
    struct timeval  tv;

    printf("pause_cb(%p) k=%s, v=%s\n", request->conn, header->key, header->val);

    tv.tv_sec  = 1;
    tv.tv_usec = 100000;

    evtimer_add(pause->timer_ev, &tv);

    return EVHTP_RES_PAUSE;
}

static evhtp_res
resume_cb(evhtp_request_t * request, evhtp_headers_t * headers, void * arg) {
    struct pauser * pause = arg;

    printf("resume_cb(%p)\n", request->conn);
    evtimer_del(pause->timer_ev);
    evhtp_request_resume(request);
    return EVHTP_RES_OK;
}

static evhtp_res
free_pause_cb(evhtp_connection_t * connection, void * arg) {
    struct pauser * pause = arg;

    printf("free_pause_cb(%p)\n", connection);

    evhtp_connection_resume(connection);
    event_free(pause->timer_ev);
    free(pause);

    return EVHTP_RES_OK;
}

static evhtp_res
pause_init_cb(evhtp_request_t * req, evhtp_path_t * path, void * arg) {
    struct pauser * pause = malloc(sizeof(struct pauser));

    printf("pause_init_cb(%p)\n", req->conn);

    pause->timer_ev = evtimer_new(req->conn->evbase,
                                  resume_request_timer, pause);
    pause->request  = req;

    evhtp_set_hook(&req->hooks, evhtp_hook_on_header, pause_cb, pause);
    evhtp_set_hook(&req->hooks, evhtp_hook_on_headers, resume_cb, pause);
    evhtp_set_hook(&req->conn->hooks, evhtp_hook_on_connection_fini, free_pause_cb, pause);
    //evhtp_set_hook(&req->hooks, evhtp_hook_on_fini, free_pause_cb, pause);

    return EVHTP_RES_OK;
}

static void
test_pause_cb(evhtp_request_t * request, void * arg) {
    printf("test_pause_cb()\n");
    evhtp_request_resume(request);
    evhtp_send_reply(request, EVHTP_RES_OK);
}

static void
test_regex(evhtp_request_t * req, void * arg) {
    evbuffer_add_printf(req->buffer_out,
                        "start = '%s', end = '%s\n",
                        req->uri->path->match_start,
                        req->uri->path->match_end);

    evhtp_send_reply(req, EVHTP_RES_OK);
}

static void
test_foo_cb(evhtp_request_t * req, void * arg ) {
    evbuffer_add_reference(req->buffer_out,
                           "test_foo_cb\n", 12, NULL, NULL);

    evhtp_send_reply(req, EVHTP_RES_OK);
}

static void
test_500_cb(evhtp_request_t * req, void * arg ) {
    evbuffer_add_reference(req->buffer_out,
                           "test_500_cb\n", 12, NULL, NULL);

    evhtp_send_reply(req, EVHTP_RES_SERVERR);
}

static void
test_bar_cb(evhtp_request_t * req, void * arg ) {
    evhtp_send_reply(req, EVHTP_RES_OK);
}

static void
test_default_cb(evhtp_request_t * req, void * arg ) {
    evbuffer_add_reference(req->buffer_out,
                           "test_default_cb\n", 16, NULL, NULL);

    evhtp_send_reply(req, EVHTP_RES_OK);
}

static evhtp_res
print_kv(evhtp_request_t * req, evhtp_header_t * hdr, void * arg) {
    evbuffer_add_printf(req->buffer_out,
                        "print_kv() key = '%s', val = '%s'\n",
                        hdr->key, hdr->val);

    return EVHTP_RES_OK;
}

static int
output_header(evhtp_header_t * header, void * arg) {
    evbuf_t * buf = arg;

    evbuffer_add_printf(buf, "print_kvs() key = '%s', val = '%s'\n",
                        header->key, header->val);
    return 0;
}

static evhtp_res
print_kvs(evhtp_request_t * req, evhtp_headers_t * hdrs, void * arg ) {
    evhtp_headers_for_each(hdrs, output_header, req->buffer_out);
    return EVHTP_RES_OK;
}

static evhtp_res
print_path(evhtp_request_t * req, evhtp_path_t * path, void * arg) {
    evbuffer_add_printf(req->buffer_out,
                        "print_path() full        = '%s'\n"
                        "             path        = '%s'\n"
                        "             file        = '%s'\n"
                        "             match start = '%s'\n"
                        "             match_end   = '%s'\n",
                        path->full, path->path, path->file,
                        path->match_start, path->match_end);

    return EVHTP_RES_OK;
}

static evhtp_res
print_data(evhtp_request_t * req, evbuf_t * buf, void * arg ) {
    evbuffer_add_printf(req->buffer_out,
                        "got %zu bytes of data\n",
                        evbuffer_get_length(buf));

    return EVHTP_RES_OK;
}

static evhtp_res
test_regex_hdrs_cb(evhtp_request_t * req, evhtp_headers_t * hdrs, void * arg ) {
    return EVHTP_RES_OK;
}

static evhtp_res
test_pre_accept(int fd, struct sockaddr * sin, int sl, void * arg) {
    uint16_t port = *(uint16_t *)arg;

    if (port > 8081) {
        return EVHTP_RES_ERROR;
    }

    return EVHTP_RES_OK;
}

static evhtp_res
set_my_connection_handlers(evhtp_connection_t * conn, void * arg ) {
    evhtp_set_hook(&conn->hooks, evhtp_hook_on_header, print_kv, "foo");
    evhtp_set_hook(&conn->hooks, evhtp_hook_on_headers, print_kvs, "bar");
    evhtp_set_hook(&conn->hooks, evhtp_hook_on_path, print_path, "baz");
    evhtp_set_hook(&conn->hooks, evhtp_hook_on_read, print_data, "derp");

    return EVHTP_RES_OK;
}

const char * optstr = "htn:a:p:r:s:c:";

const char * help   =
    "Options: \n"
    "  -h       : This help text\n"
#ifndef DISABLE_EVTHR
    "  -t       : Run requests in a thread (default: off)\n"
    "  -n <int> : Number of threads        (default: 0 if -t is off, 4 if -t is on)\n"
#endif
#ifndef DISABLE_SSL
    "  -s <pem> : Enable SSL and PEM       (default: NULL)\n"
    "  -c <ca>  : CA cert file             (default: NULL\n"
#endif
    "  -r <str> : Document root            (default: .)\n"
    "  -a <str> : Bind Address             (default: 0.0.0.0)\n"
    "  -p <int> : Bind Port                (default: 8081)\n";


int
parse_args(int argc, char ** argv) {
    extern char * optarg;
    extern int    optind;
    extern int    opterr;
    extern int    optopt;
    int           c;

    while ((c = getopt(argc, argv, optstr)) != -1) {
        switch (c) {
            case 'h':
                printf("Usage: %s [opts]\n%s", argv[0], help);
                return -1;
            case 'a':
                bind_addr = strdup(optarg);
                break;
            case 'p':
                bind_port = atoi(optarg);
                break;
#ifndef DISABLE_EVTHR
            case 't':
                use_threads = 1;
                break;
            case 'n':
                num_threads = atoi(optarg);
                break;
#endif
#ifndef DISABLE_SSL
            case 's':
                ssl_pem = strdup(optarg);
                break;
            case 'c':
                ssl_ca = strdup(optarg);
                break;
#endif
            default:
                printf("Unknown opt %s\n", optarg);
                return -1;
        } /* switch */
    }

#ifndef DISABLE_EVTHR
    if (use_threads && num_threads == 0) {
        num_threads = 4;
    }
#endif

    return 0;
} /* parse_args */

int
main(int argc, char ** argv) {
    evbase_t         * evbase = NULL;
    evhtp_t          * htp    = NULL;
    evhtp_callback_t * cb_1   = NULL;
    evhtp_callback_t * cb_2   = NULL;
    evhtp_callback_t * cb_3   = NULL;
    evhtp_callback_t * cb_4   = NULL;
    evhtp_callback_t * cb_5   = NULL;
    evhtp_callback_t * cb_6   = NULL;
    evhtp_callback_t * cb_7   = NULL;

    if (parse_args(argc, argv) < 0) {
        exit(1);
    }

#ifndef DISABLE_EVTHR
    if (use_threads) {
        evthread_use_pthreads();
    }
#endif

    evbase = event_base_new();
    htp    = evhtp_new(evbase, NULL);

    cb_1   = evhtp_set_cb(htp, "/ref", test_default_cb, "fjdkls");
    cb_2   = evhtp_set_cb(htp, "/foo", test_foo_cb, "bar");
    cb_3   = evhtp_set_cb(htp, "/foo/", test_foo_cb, "bar");
    cb_4   = evhtp_set_cb(htp, "/bar", test_bar_cb, "baz");
    cb_5   = evhtp_set_cb(htp, "/500", test_500_cb, "500");
    cb_6   = evhtp_set_regex_cb(htp, "^(/anything/).*", test_regex, NULL);
    cb_7   = evhtp_set_cb(htp, "/pause", test_pause_cb, NULL);

    /* set a callback to pause on each header for cb_7 */
    evhtp_set_hook(&cb_7->hooks, evhtp_hook_on_path, pause_init_cb, NULL);

    /* set a callback to set hooks specifically for the cb_6 callback */
    evhtp_set_hook(&cb_6->hooks, evhtp_hook_on_headers, test_regex_hdrs_cb, NULL);

    /* set a default request handler */
    evhtp_set_gencb(htp, test_default_cb, "foobarbaz");

    /* set a callback invoked before a connection is accepted */
    evhtp_set_pre_accept_cb(htp, test_pre_accept, &bind_port);

    /* set a callback to set per-connection hooks (via a post_accept cb) */
    evhtp_set_post_accept_cb(htp, set_my_connection_handlers, NULL);

#ifndef DISABLE_SSL
    if (ssl_pem != NULL) {
        evhtp_ssl_cfg_t scfg = {
            .pemfile        = ssl_pem,
            .privfile       = ssl_pem,
            .cafile         = ssl_ca,
            .ciphers        = "RC4+RSA:HIGH:+MEDIUM:+LOW",
            .ssl_opts       = SSL_OP_NO_SSLv2,
            .scache_type    = evhtp_ssl_scache_type_builtin,
            .scache_timeout = 1024,
            .scache_init    = NULL,
            .scache_add     = NULL,
            .scache_get     = NULL,
            .scache_del     = NULL,
        };

        evhtp_ssl_init(htp, &scfg);
    }
#endif

#ifndef DISABLE_EVTHR
    if (use_threads) {
        evhtp_use_threads(htp, num_threads);
    }
#endif

    evhtp_bind_socket(htp, bind_addr, bind_port);

    event_base_loop(evbase, 0);
    return 0;
} /* main */

