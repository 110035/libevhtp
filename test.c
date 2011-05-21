#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <evhtp.h>

static char * chunks[] = {
    "foo\n",
    "bar\n",
    "baz\n",
    NULL
};


static evhtp_res
_send_chunk(evhtp_request_t * req, void * arg) {
    int * idx = (int *)arg;

    if (chunks[*idx] == NULL) {
        return EVHTP_RES_DONE;
    }

    evhtp_request_make_chunk(req, chunks[*idx], strlen(chunks[*idx]));

    (*idx)++;

    return EVHTP_RES_OK;
}

static void
test_streaming(evhtp_request_t * req, void * arg) {
    int * index = calloc(sizeof(int), 1);

    evhtp_send_reply_stream(req, EVHTP_CODE_OK, _send_chunk, index);
}

static void
test_foo_cb(evhtp_request_t * req, void * arg) {
    printf("%s\n", (char *)arg);
    evhtp_send_reply(req, EVHTP_CODE_OK, "OK", NULL);
}

static void
test_500_cb(evhtp_request_t * req, void * arg) {
    evhtp_send_reply(req, EVHTP_CODE_SERVERR, "no", NULL);
}

static void
test_bar_cb(evhtp_request_t * req, void * arg) {
    printf("%s\n", (char *)arg);
    evhtp_send_reply(req, EVHTP_CODE_OK, "OK", NULL);
}

static void
test_default_cb(evhtp_request_t * req, void * arg) {
    struct evbuffer * b = evbuffer_new();

    evbuffer_add(b, "derp", 4);
    evhtp_send_reply(req, EVHTP_CODE_OK, "Everything is fine", b);
    evbuffer_free(b);
}

static evhtp_res
print_kv(evhtp_request_t * req, evhtp_hdr_t * hdr, void * arg) {
    return EVHTP_RES_OK;
}

static evhtp_res
print_kvs(evhtp_request_t * req, evhtp_hdrs_t * hdrs, void * arg) {
    return EVHTP_RES_OK;
}

static evhtp_res
print_path(evhtp_request_t * req, const char * path, void * arg) {
    if (!strncmp(path, "/derp", 5)) {
        evhtp_set_close_on(req->conn, EVHTP_CLOSE_ON_200);
    }

    return EVHTP_RES_OK;
}

static evhtp_res
print_uri(evhtp_request_t * req, const char * uri, void * arg) {
    return EVHTP_RES_OK;
}

static evhtp_res
print_data(evhtp_request_t * req, const char * data, size_t len, void * arg) {
    if (len) {
        /* printf("%.*s\n", len, data); */
        evbuffer_drain(req->buffer_in, len);
    }
    return EVHTP_RES_OK;
}

static evhtp_status
inspect_expect(evhtp_request_t * req, const char * expct_str, void * arg) {
    if (strcmp(expct_str, "100-continue")) {
        printf("Inspecting expect failed!\n");
        return EVHTP_CODE_EXPECTFAIL;
    }

    return EVHTP_CODE_CONTINUE;
}

static evhtp_res
set_my_handlers(evhtp_conn_t * conn, void * arg) {
    evhtp_set_hook(conn, EVHTP_HOOK_HDR_READ, print_kv, "foo");
    evhtp_set_hook(conn, EVHTP_HOOK_HDRS_READ, print_kvs, "bar");
    evhtp_set_hook(conn, EVHTP_HOOK_PATH_READ, print_path, "baz");
    evhtp_set_hook(conn, EVHTP_HOOK_URI_READ, print_uri, "herp");
    evhtp_set_hook(conn, EVHTP_HOOK_READ, print_data, "derp");
    evhtp_set_hook(conn, EVHTP_HOOK_ON_EXPECT, inspect_expect, "bloop");

    evhtp_set_close_on(conn,
        EVHTP_CLOSE_ON_400 |
        EVHTP_CLOSE_ON_500 |
        EVHTP_CLOSE_ON_EXPECT_ERR);

    return EVHTP_RES_OK;
}

int
main(int argc, char ** argv) {
    evbase_t * evbase = NULL;
    evhtp_t  * htp    = NULL;

    evbase = event_base_new();
    htp    = evhtp_new(evbase);

    evhtp_set_server_name(htp, "Hi there!");
    evhtp_set_cb(htp, "/ref", test_default_cb, "fjdkls");
    evhtp_set_cb(htp, "/foo", test_foo_cb, "bar");
    evhtp_set_cb(htp, "/bar", test_bar_cb, "baz");
    evhtp_set_cb(htp, "/500", test_500_cb, "500");
    evhtp_set_cb(htp, "/stream", test_streaming, NULL);
    evhtp_set_gencb(htp, test_default_cb, "foobarbaz");
    evhtp_set_post_accept_cb(htp, set_my_handlers, NULL);

    evhtp_bind_socket(htp, "0.0.0.0", 8081);

    event_base_loop(evbase, 0);
    return 0;
}

