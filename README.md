# HTTP/1.1 Calculator

A small C++ calculator server built with TCP sockets and no web framework.
The server keeps an HTTP/1.1 connection open, so several requests can use the
same socket.

## Supported requests

| Request | Status | Body |
| --- | ---: | --- |
| `GET /add?a=2&b=3` | 200 | `5` |
| `GET /sub?a=10&b=4` | 200 | `6` |
| `GET /mul?a=6&b=7` | 200 | `42` |
| `GET /div?a=9&b=3` | 200 | `3` |
| `GET /div?a=1&b=0` | 400 | `division by zero` |
| `GET /add?a=x&b=3` | 400 | `a and b must be integers` |
| `GET /pow?a=2&b=8` | 404 | `unknown operation, use /add /sub /mul /div` |
| `POST /add` | 405 | `only GET is allowed` |
| `GET /add` without `Host` | 400 | `missing Host header` |

The request reader uses `Content-Length` to consume exactly the request body.
Any bytes after the body stay in the buffer for the next request.

## Build and run

```sh
make
./server
```

The default port is `8080`. A different port can be passed as the first
argument:

```sh
./server 9000
```

## Test

Start the server, then run the test client in another terminal:

```sh
./test_client
```

The test client sends the calculator requests through one socket and checks
that the connection stays open. It also checks request bodies, pipelined
requests, `Connection: close`, and chunked bodies.

A single request can also be tested with curl:

```sh
curl -i "http://localhost:8080/add?a=2&b=3"
```

## Files

```text
src/server.cpp
src/http_parser.cpp
src/http_parser.h
src/calculator.cpp
src/calculator.h
tests/test_client.cpp
Makefile
```
