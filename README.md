# HTTP Calculator Server

A calculator that speaks HTTP/1.1 over a plain TCP socket, written in C++ with no framework.
A client can open one connection and send as many requests as it wants on it; the server
keeps the connection open and answers each request in turn.

| Request | Response |
| --- | --- |
| `GET /add?a=2&b=3` | `200` `5` |
| `GET /sub?a=10&b=4` | `200` `6` |
| `GET /mul?a=6&b=7` | `200` `42` |
| `GET /div?a=9&b=3` | `200` `3` |
| `GET /div?a=1&b=0` | `400` division by zero |
| `GET /add?a=x&b=3` | `400` a and b must be integers |
| `GET /pow?a=2&b=8` | `404` unknown operation |
| `POST /add` | `405` only GET is allowed (with `Allow: GET`) |
| `GET /add` without a `Host` header | `400` missing Host header |

## How it works

- **One thread per connection.** `main()` accepts connections and starts a `std::thread` for
  each one. The thread reads requests from its socket in a loop until the connection ends.
- **Finding where a request ends.** `RequestReader` keeps a buffer of bytes received on the
  connection. It reads the request line and the headers up to the empty line, then takes
  exactly `Content-Length` bytes of body from the buffer. Whatever comes after those bytes
  stays in the buffer and is the start of the next request.
- **Pipelining.** Because leftover bytes are kept, several requests sent in one write are
  answered one by one, in the order they were sent.
- **Chunked bodies.** `Transfer-Encoding: chunked` bodies are decoded chunk by chunk until the
  `0` chunk, so the request after them is found correctly as well.
- **Connection: close.** If the client sends `Connection: close` (or uses HTTP/1.0 without
  `keep-alive`), the reply says `Connection: close` and the server closes the socket.
- **Idle timeout, 10 seconds by default.** The socket has `SO_RCVTIMEO` set, so a connection
  that sends nothing for 10 seconds is closed and its thread ends. Every open connection keeps
  a thread busy, so connections that are left open and unused should not stay forever. 10
  seconds is still much longer than the gap between requests of a client that is actually
  using the connection.
- **Errors.** `400`, `404` and `405` keep the connection open. A request that cannot be parsed
  at all (bad request line, header without `:`, invalid `Content-Length`) gets `400` and the
  connection is closed, because the server can no longer tell where the next request starts.
  Headers over 8 KB get `431`, bodies over 1 MB get `413`.
- **Numbers.** `a` and `b` must be whole numbers (a leading `-` is allowed). Results that do not
  fit in 64 bits give `400`. Division is integer division.

## Project structure

```
Folder_1/
├── src/
│   ├── server.cpp        # creates the socket, accepts connections, one thread per client
│   ├── http_parser.h
│   ├── http_parser.cpp   # reads requests from the socket: request line, headers, body
│   ├── calculator.h
│   └── calculator.cpp    # /add /sub /mul /div and all the error checks
├── tests/
│   └── test_client.cpp   # sends requests over TCP and prints the answers
├── Makefile
├── README.md
└── .gitignore
```

## Build

Needs `g++` (or `clang++`) with C++11 and `make`, on Linux or macOS.

```
make
```

This creates two programs, `server` and `test_client`. Without make:

```
g++ -std=c++11 -Wall -O2 -pthread -o server src/server.cpp src/http_parser.cpp src/calculator.cpp
g++ -std=c++11 -Wall -O2 -pthread -o test_client tests/test_client.cpp
```

## Start the server

```
./server                # port 8080, idle timeout 10 s
./server 8080 5         # port 8080, idle timeout 5 s
./server 9000           # another port if 8080 is busy
```

The server prints one line for every connection and every request it answers. Stop it with
`Ctrl+C`.

## Connect to the server

Open a second terminal while the server is running.

With curl:

```
curl -i "http://localhost:8080/add?a=2&b=3"
```

By hand with netcat. `-c` (macOS) or `-C` (Linux) makes netcat send `\r\n` line endings:

```
nc -c localhost 8080          # macOS
nc -C localhost 8080          # Linux
```

Then type a request and press Enter twice:

```
GET /add?a=2&b=3 HTTP/1.1
Host: localhost

```

The answer appears and the connection stays open, so you can type the next request on the
same connection.

## Run all queries at once

The test client opens connections to the server, sends the requests and prints every answer:

```
./test_client           # server on port 8080
./test_client 9000      # server on another port
```

It sends all requests from the table above on one connection, then a request with a body
followed by another request, a chunked body, six pipelined requests, and a request with
`Connection: close`.

The same nine requests on one connection with curl:

```
curl -s -w ' (HTTP %{http_code})\n' \
  "http://localhost:8080/add?a=2&b=3" "http://localhost:8080/sub?a=10&b=4" \
  "http://localhost:8080/mul?a=6&b=7" "http://localhost:8080/div?a=9&b=3" \
  "http://localhost:8080/div?a=1&b=0" "http://localhost:8080/add?a=x&b=3" \
  "http://localhost:8080/pow?a=2&b=8" \
  --next -s -w ' (HTTP %{http_code})\n' -X POST "http://localhost:8080/add" \
  --next -s -w ' (HTTP %{http_code})\n' -H "Host:" "http://localhost:8080/add?a=2&b=3"
```

The server output shows that all of them came in on the same connection.

## Run the queries one by one

```
curl -i "http://localhost:8080/add?a=2&b=3"
curl -i "http://localhost:8080/sub?a=10&b=4"
curl -i "http://localhost:8080/mul?a=6&b=7"
curl -i "http://localhost:8080/div?a=9&b=3"
curl -i "http://localhost:8080/div?a=1&b=0"
curl -i "http://localhost:8080/add?a=x&b=3"
curl -i "http://localhost:8080/pow?a=2&b=8"
curl -i -X POST "http://localhost:8080/add"
curl -i -H "Host:" "http://localhost:8080/add?a=2&b=3"
```

(`-H "Host:"` tells curl to leave out the Host header.)

## More commands

Keep-alive, two requests on one connection (curl prints `Re-using existing connection`):

```
curl -v "http://localhost:8080/add?a=2&b=3" "http://localhost:8080/mul?a=6&b=7"
```

Pipelining, six requests sent in one write:

```
printf 'GET /add?a=2&b=3 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /sub?a=10&b=4 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /mul?a=6&b=7 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /div?a=1&b=0 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /pow?a=2&b=8 HTTP/1.1\r\nHost: localhost\r\n\r\nPOST /add HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc localhost 8080
```

Request with a 7 byte body, and the next request right behind it:

```
printf 'POST /add HTTP/1.1\r\nHost: localhost\r\nContent-Length: 7\r\n\r\na=1&b=2GET /mul?a=3&b=4 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc localhost 8080
```

Chunked body:

```
printf 'POST /add HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nwiki\r\n5\r\npedia\r\n0\r\n\r\nGET /div?a=9&b=3 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc localhost 8080
```

Connection: close and HTTP/1.0:

```
curl -i -H "Connection: close" "http://localhost:8080/add?a=1&b=1"
curl -i --http1.0 "http://localhost:8080/add?a=1&b=2"
```

Idle timeout. Start the server with a short timeout, then let the client send one request
and wait:

```
./server 8080 5
./test_client 8080 idle
```
