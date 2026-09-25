// HTTP/1.1 calculator server.
// The main thread only accepts connections. Every connection gets its own std::thread,
// which keeps answering requests on that socket until the client closes it, sends
// Connection: close, sends something that can't be parsed, or stays idle too long.
//
// usage: ./server [port] [idle-timeout-seconds]

#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "calculator.h"
#include "http_parser.h"

static int idleTimeout = 10;
static std::mutex logMutex;

static const char *statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        case 501: return "Not Implemented";
    }
    return "Error";
}

static std::string buildResponse(const Response &res, bool keepOpen) {
    std::ostringstream out;
    out << "HTTP/1.1 " << res.status << " " << statusText(res.status) << "\r\n"
        << "Content-Type: text/plain\r\n"
        << "Content-Length: " << res.body.size() << "\r\n"
        << "Connection: " << (keepOpen ? "keep-alive" : "close") << "\r\n"
        << res.extraHeaders << "\r\n"
        << res.body;
    return out.str();
}

static bool sendAll(int fd, const std::string &data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

static void logLine(int fd, const std::string &what) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << "[conn " << fd << "] " << what << std::endl;
}

static void handleClient(int fd) {
    // recv() gives up when nothing arrives for idleTimeout seconds
    struct timeval tv;
    tv.tv_sec = idleTimeout;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    logLine(fd, "connected");
    RequestReader reader(fd);
    int answered = 0;

    while (true) {
        Request req;
        int err = reader.next(req);
        if (err == -1) break;
        if (err > 0) {
            // we can't tell where the next request would start, so close
            Response res;
            res.status = err;
            res.body = statusText(err);
            sendAll(fd, buildResponse(res, false));
            logLine(fd, "bad request -> " + std::to_string(err));
            break;
        }

        Response res = calculate(req);
        bool keepOpen = keepAlive(req);
        answered++;
        std::string result = std::to_string(res.status);
        if (res.status == 200) result += " " + res.body;
        logLine(fd, req.method + " " + req.target + " -> " + result);
        if (!sendAll(fd, buildResponse(res, keepOpen)) || !keepOpen) break;
    }

    logLine(fd, "closed, answered " + std::to_string(answered));
    close(fd);
}

int main(int argc, char **argv) {
    int port = 8080;
    if (argc > 1) port = atoi(argv[1]);
    if (argc > 2) idleTimeout = atoi(argv[2]);
    if (port <= 0 || port > 65535 || idleTimeout <= 0) {
        std::cerr << "usage: " << argv[0] << " [port] [idle-timeout-seconds]" << std::endl;
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);   // a client that disconnects early must not kill the server

    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        perror("socket");
        return 1;
    }
    int yes = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(listenFd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(listenFd, 64) < 0) {
        perror("listen");
        return 1;
    }
    std::cout << "server listening on port " << port << ", idle timeout " << idleTimeout
              << "s" << std::endl;

    while (true) {
        int fd = accept(listenFd, 0, 0);
        if (fd < 0) {
            if (errno != EINTR) perror("accept");
            continue;
        }
        std::thread(handleClient, fd).detach();
    }
}
