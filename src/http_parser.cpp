#include "http_parser.h"

#include <sys/socket.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <sstream>

static const size_t MAX_LINE = 8192;
static const size_t MAX_BODY = 1 << 20;

std::string toLower(std::string s) {
    for (size_t i = 0; i < s.size(); i++) s[i] = (char)tolower((unsigned char)s[i]);
    return s;
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

bool Request::hasHeader(const std::string &name) const {
    return headers.count(name) > 0;
}

std::string Request::header(const std::string &name) const {
    std::map<std::string, std::string>::const_iterator it = headers.find(name);
    return it == headers.end() ? "" : it->second;
}

bool keepAlive(const Request &req) {
    std::string conn = toLower(req.header("connection"));
    if (conn.find("close") != std::string::npos) return false;
    if (req.version == "HTTP/1.0") return conn.find("keep-alive") != std::string::npos;
    return true;
}

static void parseQuery(const std::string &query, std::map<std::string, std::string> &params) {
    std::istringstream parts(query);
    std::string pair;
    while (std::getline(parts, pair, '&')) {
        size_t eq = pair.find('=');
        if (eq != std::string::npos) params[pair.substr(0, eq)] = pair.substr(eq + 1);
    }
}

// One more recv() into the buffer. False on EOF, error or idle timeout
// (the socket has SO_RCVTIMEO set, so recv() gives up after the idle time).
bool RequestReader::fill() {
    char tmp[4096];
    while (true) {
        ssize_t n = recv(fd_, tmp, sizeof(tmp), 0);
        if (n > 0) {
            buf_.append(tmp, n);
            return true;
        }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
}

// One line without the CRLF at the end.
bool RequestReader::readLine(std::string &line) {
    while (true) {
        size_t nl = buf_.find("\r\n");
        if (nl != std::string::npos) {
            line = buf_.substr(0, nl);
            buf_.erase(0, nl + 2);
            return true;
        }
        if (buf_.size() > MAX_LINE) return false;
        if (!fill()) return false;
    }
}

// Takes exactly n bytes off the front of the buffer, not one more.
bool RequestReader::readBytes(size_t n, std::string &out) {
    while (buf_.size() < n)
        if (!fill()) return false;
    out = buf_.substr(0, n);
    buf_.erase(0, n);
    return true;
}

// Transfer-Encoding: chunked
// every chunk is "<size in hex>\r\n<data>\r\n", a chunk of size 0 is the last one,
// after it come optional trailer lines and an empty line.
int RequestReader::readChunked(std::string &body) {
    while (true) {
        std::string line;
        if (!readLine(line)) return -1;
        std::string hex = trim(line.substr(0, line.find(';')));
        if (hex.empty() || hex.size() > 8) return 400;
        for (size_t i = 0; i < hex.size(); i++)
            if (!isxdigit((unsigned char)hex[i])) return 400;
        size_t size = strtoul(hex.c_str(), 0, 16);

        if (size == 0) {
            do {
                if (!readLine(line)) return -1;
            } while (!line.empty());
            return 0;
        }

        if (body.size() + size > MAX_BODY) return 413;
        std::string chunk, crlf;
        if (!readBytes(size, chunk) || !readBytes(2, crlf)) return -1;
        if (crlf != "\r\n") return 400;
        body += chunk;
    }
}

int RequestReader::next(Request &req) {
    std::string line;
    do {   // empty lines before the request line are allowed
        if (!readLine(line)) return buf_.size() > MAX_LINE ? 431 : -1;
    } while (line.empty());

    // request line: METHOD TARGET VERSION
    std::istringstream first(line);
    std::string extra;
    if (!(first >> req.method >> req.target >> req.version) || (first >> extra)) return 400;
    if (req.version != "HTTP/1.1" && req.version != "HTTP/1.0") return 400;

    size_t q = req.target.find('?');
    req.path = req.target.substr(0, q);
    if (q != std::string::npos) parseQuery(req.target.substr(q + 1), req.params);

    // headers until the empty line
    size_t headerBytes = 0;
    while (true) {
        if (!readLine(line)) return buf_.size() > MAX_LINE ? 431 : -1;
        if (line.empty()) break;
        headerBytes += line.size();
        if (headerBytes > MAX_LINE) return 431;

        size_t colon = line.find(':');
        if (colon == std::string::npos) return 400;
        req.headers[toLower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }

    // body: chunked, or exactly Content-Length bytes
    if (req.hasHeader("transfer-encoding")) {
        if (toLower(req.header("transfer-encoding")) != "chunked") return 501;
        return readChunked(req.body);
    }

    size_t length = 0;
    if (req.hasHeader("content-length")) {
        std::string value = req.header("content-length");
        if (value.empty()) return 400;
        for (size_t i = 0; i < value.size(); i++)
            if (!isdigit((unsigned char)value[i])) return 400;
        if (value.size() > 9) return 413;   // way over the limit anyway
        length = strtoul(value.c_str(), 0, 10);
        if (length > MAX_BODY) return 413;
    }
    if (!readBytes(length, req.body)) return -1;
    return 0;
}
