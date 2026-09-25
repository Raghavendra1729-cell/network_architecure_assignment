#ifndef CALCULATOR_H
#define CALCULATOR_H

#include <string>

#include "http_parser.h"

struct Response {
    int status = 200;
    std::string body;
    std::string extraHeaders;
};

// Only an optional minus sign followed by digits, so "2.5", "x" and "" are rejected.
bool parseInteger(const std::string &s, long long &out);

// Checks the request and works out the answer:
// 400 no Host / bad numbers / division by zero, 404 unknown path, 405 not GET.
Response calculate(const Request &req);

#endif
