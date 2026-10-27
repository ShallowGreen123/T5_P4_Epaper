#pragma once

#include <string>

class String {
public:
    String() = default;
    String(const char *s) : value_(s ? s : "") {}
    String(const std::string &s) : value_(s) {}

    const char *c_str() const { return value_.c_str(); }
    size_t length() const { return value_.length(); }
    bool isEmpty() const { return value_.empty(); }
    operator std::string() const { return value_; }

private:
    std::string value_;
};
