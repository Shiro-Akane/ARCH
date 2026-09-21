#pragma once
#include <iostream>
#include <streambuf>
#include <string>

namespace arch::api::detail {
class BoundedLog : public std::streambuf {
public:
    std::string text;
    bool truncated = false;
    std::string message() const {
        std::string result = text;
        // Truncation may cut a UTF-8 character. Drop the final multibyte
        // sequence rather than emitting malformed JSON text.
        if (truncated) {
            while (!result.empty() && (static_cast<unsigned char>(result.back()) & 0xc0) == 0x80)
                result.pop_back();
            if (!result.empty() && static_cast<unsigned char>(result.back()) >= 0xc0)
                result.pop_back();
        }
        return result;
    }
protected:
    int_type overflow(int_type c) override {
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            if (text.size() < 16384) text.push_back(traits_type::to_char_type(c));
            else truncated = true;
        }
        return traits_type::not_eof(c);
    }
};
class CaptureLogs {
public:
    BoundedLog info, warning;
private:
    std::streambuf *out_, *err_;
public:
    CaptureLogs() : out_(std::cout.rdbuf(&info)), err_(std::cerr.rdbuf(&warning)) {}
    ~CaptureLogs() { std::cout.rdbuf(out_); std::cerr.rdbuf(err_); }
};
} // namespace arch::api::detail
