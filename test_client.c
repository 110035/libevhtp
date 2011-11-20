#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <evhtp.h>

static void
request_cb(evhtp_request_t * req, void * arg) {
    printf("hi %zu\n", evbuffer_get_length(req->buffer_in));
    printf("%.*s\n", evbuffer_get_length(req->buffer_in), 
	    evbuffer_pullup(req->buffer_in, -1));
}

int
main(int argc, char ** argv) {
    evbase_t           * evbase;
    evhtp_connection_t * conn;
    evhtp_request_t    * request;

    evbase  = event_base_new();
    conn    = evhtp_connection_new(evbase, "75.126.169.52", 80);
    request = evhtp_request_new(request_cb, NULL);

    evhtp_headers_add_header(request->headers_out,
                             evhtp_header_new("Host", "ieatfood.net", 0, 0));

    evhtp_make_request(conn, request, htp_method_GET, "/");

    event_base_loop(evbase, 0);

    return 0;
}

