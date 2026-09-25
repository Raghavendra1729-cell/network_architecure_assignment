#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <map>
#include <string>

struct Request {
    std::string method;
    std::string target;                           // path with the query string
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;   // header names in lowercase
    std::map<std::string, std::string> params;    // from the query string
    std::string body;

    bool hasHeader(const std::string &name) const;
    std::string header(const std::string &name) const;
};

// Reads requests one after another from a connected socket. Bytes that arrive after
// the end of a request stay in the buffer and are the start of the next request.
class RequestReader {
public:
    explicit RequestReader(int fd) : fd_(fd) {}

    // 0  - a whole request was read into req
    // -1 - the client closed the connection or was idle for too long
    // otherwise the HTTP status to send before closing (the request is broken)
    int next(Request &req);

private:
    int fd_;
    std::string buf_;

    bool fill();
    bool readLine(std::string &line);
    bool readBytes(size_t n, std::string &out);
    int readChunked(std::string &body);
};

std::string toLower(std::string s);
std::string trim(const std::string &s);

// HTTP/1.1 keeps the connection open unless the client says close,
// HTTP/1.0 closes unless the client asks for keep-alive.
bool keepAlive(const Request &req);

#endif
