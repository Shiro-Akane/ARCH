/**
 * @file SessionInput.h
 * @brief Parse a bounded flat JSON session envelope with unique string/integer members.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Parse a bounded flat JSON session envelope with unique string/integer members.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include <charconv>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>

#include "api/protocol/RequestInput.h"

namespace arch::api::detail {
// Strict flat JSON transport envelope, not a configuration parser. Only string
// and integer members are in protocol v1. Reject duplicates and unsupported
// values instead of silently changing the meaning of a request.
using SessionValue = std::variant<std::string, std::int64_t>;
using SessionObject = std::map<std::string, SessionValue>;
class SessionInput {
    std::string_view text_;
    std::size_t pos_ = 0;
    /** Reject malformed or unsupported session syntax. */
    [[noreturn]] static void fail() { throw std::invalid_argument("Expected a flat JSON object with unique string/integer members"); }
    /** Skip permitted horizontal whitespace without crossing a record. */
    void space() { while (pos_ < text_.size() && (text_[pos_]==' ' || text_[pos_]=='\t' || text_[pos_]=='\r')) ++pos_; }
    /** Consume an expected delimiter after whitespace. */
    bool take(char c) { space(); if (pos_ < text_.size() && text_[pos_]==c) { ++pos_; return true; } return false; }
    /** Parse exactly four hexadecimal digits for a JSON Unicode escape. */
    unsigned hex4() {
        unsigned v = 0;
        for (int i=0;i<4;++i) {
            if (pos_ == text_.size()) fail();
            const char c=text_[pos_++]; unsigned n;
            if (c>='0' && c<='9') n=c-'0';
            else if (c>='a' && c<='f') n=c-'a'+10;
            else if (c>='A' && c<='F') n=c-'A'+10;
            else fail();
            v=v*16+n;
        }
        return v;
    }
    /** Encode one validated Unicode scalar as UTF-8. */
    static void utf8(std::string& s, unsigned c) {
        if (c<0x80) s+=char(c);
        else if (c<0x800) { s+=char(0xc0|(c>>6)); s+=char(0x80|(c&63)); }
        else if (c<0x10000) { s+=char(0xe0|(c>>12)); s+=char(0x80|((c>>6)&63)); s+=char(0x80|(c&63)); }
        else { s+=char(0xf0|(c>>18)); s+=char(0x80|((c>>12)&63)); s+=char(0x80|((c>>6)&63)); s+=char(0x80|(c&63)); }
    }
    /** Decode a JSON string, surrogate pair and escapes, then validate UTF-8. */
    std::string string() {
        if (!take('"')) fail();
        std::string result;
        while (pos_<text_.size()) {
            unsigned char c=text_[pos_++];
            if (c=='"') {
                if (!ValidUtf8(result)) throw std::invalid_argument("Request strings must be UTF-8 without NUL");
                return result;
            }
            if (c<0x20) fail();
            if (c!='\\') { result+=char(c); continue; }
            if (pos_==text_.size()) fail();
            switch (text_[pos_++]) {
                case '"': result+='"'; break;
                case '\\': result+='\\'; break;
                case '/': result+='/'; break;
                case 'b': result+='\b'; break;
                case 'f': result+='\f'; break;
                case 'n': result+='\n'; break;
                case 'r': result+='\r'; break;
                case 't': result+='\t'; break;
                case 'u': {
                    unsigned value=hex4();
                    if (value>=0xd800 && value<=0xdbff) {
                        if (pos_+2>text_.size() || text_.substr(pos_,2)!="\\u") fail();
                        pos_+=2; const unsigned low=hex4();
                        if (low<0xdc00 || low>0xdfff) fail();
                        value=0x10000+((value-0xd800)<<10)+(low-0xdc00);
                    } else if (value>=0xdc00 && value<=0xdfff) fail();
                    utf8(result,value); break;
                }
                default: fail();
            }
        }
        fail();
    }
    /** Parse the protocol string or signed integer value. */
    SessionValue value() {
        space();
        if (pos_<text_.size() && text_[pos_]=='"') return string();
        const auto start=pos_;
        if (pos_<text_.size() && text_[pos_]=='-') ++pos_;
        const auto digits=pos_;
        while (pos_<text_.size() && text_[pos_]>='0' && text_[pos_]<='9') ++pos_;
        if (pos_==digits || (pos_-digits>1 && text_[digits]=='0')) fail();
        std::int64_t result;
        const auto parsed=std::from_chars(text_.data()+start,text_.data()+pos_,result);
        if (parsed.ec!=std::errc{} || parsed.ptr!=text_.data()+pos_) fail();
        return result;
    }
public:
    /** Borrow bounded request text for one parser lifetime. */
    explicit SessionInput(std::string_view text) : text_(text) {}
    /** Return a flat object and reject duplicate keys or trailing text. */
    SessionObject parse() {
        SessionObject result;
        if (!take('{')) fail();
        if (!take('}')) {
            do {
                auto key=string();
                if (!take(':')) fail();
                auto v=value();
                if (!result.emplace(std::move(key),std::move(v)).second) fail();
            } while (take(','));
            if (!take('}')) fail();
        }
        space(); if (pos_!=text_.size()) fail();
        return result;
    }
};
}
