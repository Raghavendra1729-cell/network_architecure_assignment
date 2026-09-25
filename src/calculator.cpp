#include "calculator.h"

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>

bool parseInteger(const std::string &s, long long &out) {
    size_t i = (!s.empty() && s[0] == '-') ? 1 : 0;
    if (i == s.size()) return false;
    for (size_t k = i; k < s.size(); k++)
        if (!isdigit((unsigned char)s[k])) return false;

    errno = 0;
    long long v = strtoll(s.c_str(), 0, 10);
    if (errno == ERANGE) return false;
    out = v;
    return true;
}

static Response error(int status, const std::string &message) {
    Response r;
    r.status = status;
    r.body = message;
    return r;
}

Response calculate(const Request &req) {
    if (req.version == "HTTP/1.1" && !req.hasHeader("host"))
        return error(400, "missing Host header");

    char op;
    if (req.path == "/add") op = '+';
    else if (req.path == "/sub") op = '-';
    else if (req.path == "/mul") op = '*';
    else if (req.path == "/div") op = '/';
    else return error(404, "unknown operation, use /add /sub /mul /div");

    if (req.method != "GET") {
        Response r = error(405, "only GET is allowed");
        r.extraHeaders = "Allow: GET\r\n";
        return r;
    }

    if (!req.params.count("a") || !req.params.count("b"))
        return error(400, "a and b are required");

    long long a, b;
    if (!parseInteger(req.params.at("a"), a) || !parseInteger(req.params.at("b"), b))
        return error(400, "a and b must be integers");

    long long result = 0;
    bool overflow = false;
    switch (op) {
        case '+': overflow = __builtin_add_overflow(a, b, &result); break;
        case '-': overflow = __builtin_sub_overflow(a, b, &result); break;
        case '*': overflow = __builtin_mul_overflow(a, b, &result); break;
        case '/':
            if (b == 0) return error(400, "division by zero");
            if (a == LLONG_MIN && b == -1) overflow = true;
            else result = a / b;
            break;
    }
    if (overflow) return error(400, "result is out of range");

    Response r;
    r.body = std::to_string(result);
    return r;
}
