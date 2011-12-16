# Libevhtp

This document describes details on using the evhtp API

## Required Dependencies
* [gcc](http://gcc.gnu.org/)
* [Libevent2](http://libevent.org)

## Optional Dependencies
* [OpenSSL](http://openssl.org)
* pthreads

## Overview

Libevhtp was created as a replacement API for Libevent's current HTTP API.  The reality of libevent's http interface is that it was created as a JIT server, meaning the developer never thought of it being used for creating a full-fledged HTTP service. Infact I am under the impression that the libevent http API was designed almost as an example of what you can do with libevent. It's not Apache in a box, but more and more developers are attempting to use it as so.

### Libevent's HTTP pitfalls

* It was not designed to be a fully functional HTTP server.
* The code is messy, abstractions are almost non-existent, and feature-creep has made long-term maintainability very hard.
* The parsing code is slow and requires data to be buffered before a full parse can be completed. This results in extranious memory usage and lots of string comparison functions.
* There is no method for a user to access various parts of the request processing cycle. For example if the "Content-Length" header has a value of 50000, your callback is not executed until all 50000 bytes have been read.
* Setting callback URI's do exact matches; meaning if you set a callback for "/foo/", requests for "/foo/bar/" are ignored.
* Creating an HTTPS server is hard, it requires a bunch of work to be done on the underlying bufferevents.
* As far as I know, streaming data back to a client is hard, if not impossible without messing with underlying bufferevents.
* It's confusing to work with, this is probably due to the lack of proper documentation.

Libevhtp attempts to address these problems along with a wide variety of cool mechanisms allowing a developer to have complete control over your server operations. This is not to say the API cannot be used in a very simplistic manner - a developer can easily create a backwards compatible version of libevent's HTTP server to libevhtp.

### A bit about the architecture of libevhtp

#### Bootstrapping 

1.	Create a parent evhtp_t structure.
2.	Assign callbacks to the parent for specific URIs or posix-regex based URI's
3.	Optionally assign per-connection hooks (see hooks) to the callbacks.
4.	Optionally assign pre-accept and post-accept callbacks for incoming connections.	
5.	Optionally enable built-in threadpool for connection handling (lock-free, and non-blocking).
6.	Optionally morph your server to HTTPS.
7.	Start the evhtp listener.

#### Request handling.

1.	Optionally assign per-request hooks (see hooks) for request.
2.	Optionally deal with pre-accept and post-accept callbacks if they exist, allowing for a connection to be rejected if the function deems it as unacceptable.
2.	Deal with either per-connection or per-request hook callbacks if they exist.
3.	Once the request has been fully processed, inform evhtp to send a reply.
