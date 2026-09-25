// Client that sends requests to the calculator server and prints every answer.
// usage: ./test_client [port]        (start ./server first)
//        ./test_client [port] idle   (only checks the idle timeout)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int port = 8080;

struct Reply {
    int status = -1;
    std::string headers;
    std::string body;
};

int connectToServer() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        std::cerr << "could not connect to 127.0.0.1:" << port << ", start ./server first"
                  << std::endl;
        exit(1);
    }
    // don't hang forever if the server never answers
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

void sendAll(int fd, const std::string &data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return;
        sent += n;
    }
}

// Reads one response using its Content-Length. leftover keeps the bytes that
// already belong to the next response.
Reply readReply(int fd, std::string &leftover) {
    Reply r;
    char buf[4096];
    size_t end;
    while ((end = leftover.find("\r\n\r\n")) == std::string::npos) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return r;
        leftover.append(buf, n);
    }
    r.headers = leftover.substr(0, end + 2);
    leftover.erase(0, end + 4);
    r.status = atoi(r.headers.c_str() + 9);   // skip "HTTP/1.1 "

    size_t length = 0;
    size_t pos = r.headers.find("Content-Length:");
    if (pos != std::string::npos) length = atoi(r.headers.c_str() + pos + 15);
    while (leftover.size() < length) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return r;
        leftover.append(buf, n);
    }
    r.body = leftover.substr(0, length);
    leftover.erase(0, length);
    return r;
}

// Open = no EOF from the server within 300 ms.
bool stillOpen(int fd) {
    pollfd p;
    p.fd = fd;
    p.events = POLLIN;
    if (poll(&p, 1, 300) == 0) return true;
    char c;
    return recv(fd, &c, 1, MSG_PEEK) > 0;
}

void show(const std::string &label, const Reply &r) {
    std::cout << "  " << std::left << std::setw(28) << label << " -> ";
    if (r.status < 0) std::cout << "no response" << std::endl;
    else std::cout << r.status << " " << r.body << std::endl;
}

std::string get(const std::string &target) {
    return "GET " + target + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
}

const char *yesNo(bool b) { return b ? "True" : "False"; }

void oneSocket() {
    std::cout << "One connection, one request after the other" << std::endl;
    std::vector<std::pair<std::string, std::string> > requests = {
        {"GET /add?a=2&b=3", get("/add?a=2&b=3")},
        {"GET /sub?a=10&b=4", get("/sub?a=10&b=4")},
        {"GET /mul?a=6&b=7", get("/mul?a=6&b=7")},
        {"GET /div?a=9&b=3", get("/div?a=9&b=3")},
        {"GET /div?a=1&b=0", get("/div?a=1&b=0")},
        {"GET /add?a=x&b=3", get("/add?a=x&b=3")},
        {"GET /pow?a=2&b=8", get("/pow?a=2&b=8")},
        {"POST /add", "POST /add HTTP/1.1\r\nHost: localhost\r\n\r\n"},
        {"GET /add (no Host)", "GET /add?a=2&b=3 HTTP/1.1\r\n\r\n"},
    };

    int fd = connectToServer();
    std::string leftover;
    int count = 0;
    for (size_t i = 0; i < requests.size(); i++) {
        sendAll(fd, requests[i].second);
        Reply r = readReply(fd, leftover);
        if (r.status > 0) count++;
        show(requests[i].first, r);
    }
    std::cout << "  socket still open: " << yesNo(stillOpen(fd)) << std::endl;
    std::cout << "  1 TCP handshake, " << count << " responses" << std::endl;
    close(fd);
}

void contentLength() {
    std::cout << std::endl << "Request body (Content-Length) followed by another request"
              << std::endl;
    int fd = connectToServer();
    std::string leftover;

    // the body looks like a request, but it is only 7 bytes of body
    sendAll(fd, "POST /add HTTP/1.1\r\nHost: localhost\r\nContent-Length: 7\r\n\r\na=1&b=2" +
                    get("/mul?a=3&b=4"));
    show("POST /add, 7 byte body", readReply(fd, leftover));
    show("GET /mul?a=3&b=4", readReply(fd, leftover));

    // body arriving in two parts
    sendAll(fd, "POST /sub HTTP/1.1\r\nHost: localhost\r\nContent-Length: 10\r\n\r\nhello");
    usleep(200000);
    sendAll(fd, "world" + get("/sub?a=1&b=5"));
    show("POST /sub, body in 2 parts", readReply(fd, leftover));
    show("GET /sub?a=1&b=5", readReply(fd, leftover));
    std::cout << "  socket still open: " << yesNo(stillOpen(fd)) << std::endl;
    close(fd);
}

void chunked() {
    std::cout << std::endl << "Chunked request body" << std::endl;
    int fd = connectToServer();
    std::string leftover;
    sendAll(fd, "POST /add HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
                "4\r\nwiki\r\n5\r\npedia\r\n0\r\n\r\n" + get("/div?a=-9&b=2"));
    show("POST /add, chunked body", readReply(fd, leftover));
    show("GET /div?a=-9&b=2", readReply(fd, leftover));
    std::cout << "  socket still open: " << yesNo(stillOpen(fd)) << std::endl;
    close(fd);
}

void pipelining() {
    std::cout << std::endl << "Pipelining: six requests sent in one write" << std::endl;
    int fd = connectToServer();
    sendAll(fd, get("/add?a=2&b=3") + get("/sub?a=10&b=4") + get("/mul?a=6&b=7") +
                    get("/div?a=1&b=0") + get("/pow?a=2&b=8") +
                    "POST /add HTTP/1.1\r\nHost: localhost\r\n\r\n");
    const char *labels[] = {"GET /add?a=2&b=3", "GET /sub?a=10&b=4", "GET /mul?a=6&b=7",
                            "GET /div?a=1&b=0", "GET /pow?a=2&b=8", "POST /add"};
    std::string leftover;
    for (int i = 0; i < 6; i++) show(labels[i], readReply(fd, leftover));
    std::cout << "  socket still open: " << yesNo(stillOpen(fd)) << std::endl;
    close(fd);
}

void connectionClose() {
    std::cout << std::endl << "Connection: close" << std::endl;
    int fd = connectToServer();
    std::string leftover;
    sendAll(fd, "GET /add?a=1&b=1 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    show("GET /add?a=1&b=1", readReply(fd, leftover));
    std::cout << "  socket still open: " << yesNo(stillOpen(fd)) << std::endl;
    close(fd);
}

double now() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

void idleTimeout() {
    std::cout << "Idle timeout" << std::endl;
    int fd = connectToServer();
    std::string leftover;
    sendAll(fd, get("/add?a=1&b=2"));
    show("GET /add?a=1&b=2", readReply(fd, leftover));

    // now send nothing and wait until the server hangs up
    std::cout << "  sending nothing, waiting for the server to close..." << std::endl;
    struct timeval tv;
    tv.tv_sec = 120;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    double start = now();
    char c;
    ssize_t n = recv(fd, &c, 1, 0);
    if (n == 0)
        printf("  server closed the connection after %.1f seconds\n", now() - start);
    else
        std::cout << "  server did not close the connection" << std::endl;
    close(fd);
}

int main(int argc, char **argv) {
    if (argc > 1) port = atoi(argv[1]);
    if (argc > 2 && std::string(argv[2]) == "idle") {
        idleTimeout();
        return 0;
    }
    oneSocket();
    contentLength();
    chunked();
    pipelining();
    connectionClose();
    return 0;
}
