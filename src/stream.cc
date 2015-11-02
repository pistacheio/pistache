/* stream.cc
   Mathieu Stefani, 05 September 2015
   
*/

#include "stream.h"
#include <algorithm>
#include <iostream>

void
BasicStreamBuf::setArea(const char* begin, const char* end, const char* brk) {
    this->begin = begin;
    this->end = end;
    this->brk = brk;
}

StreamBuf::StreamBuf(const char* begin, const char* end) {
    setArea(begin, end, end);
}

StreamBuf::StreamBuf(const char *begin, const char* end, const char* brk) {
    setArea(begin, end, brk);
}

StreamBuf::StreamBuf(const char* begin, size_t len) {
   setArea(begin, begin + len, begin + len);
} 

bool
StreamCursor::advance(size_t count) {
    if (value + count > buf.totalAvailable())
        return false;

    else if (value + count > buf.breakAvailable())
        return false;

    value += count;
    return true;
}

bool
StreamCursor::eol() const {
    return buf.begin[value] == CR && next() == LF;
}

bool
StreamCursor::eof() const {
    return remaining() == 0;
}

int
StreamCursor::next() const {
    if (value + 1 >= buf.totalAvailable())
        return Eof;

    else if (value + 1 > buf.breakAvailable())
        return Eof;

    return buf.begin[value + 1];
}

char
StreamCursor::current() const {
    return buf.begin[value];
}

const char*
StreamCursor::offset() const {
    return buf.begin + value;
}

const char*
StreamCursor::offset(size_t off) const {
    return buf.begin + off;
}

size_t
StreamCursor::diff(size_t other) const {
    return value - other;
}

size_t
StreamCursor::diff(const StreamCursor& other) const {
    return other.value - value;
}

size_t
StreamCursor::remaining() const {
    return buf.breakAvailable() - value;
}

void
StreamCursor::reset() {
    value = 0;
}

bool
match_raw(const void* buf, size_t len, StreamCursor& cursor) {
    if (cursor.remaining() < len)
        return false;

    if (memcmp(cursor.offset(), buf, len) == 0) {
        cursor.advance(len);
        return true;
    }

    return false;
}

bool
match_literal(char c, StreamCursor& cursor, CaseSensitivity cs) {
    if (cursor.eof())
        return false;

    char lhs = (cs == CaseSensitivity::Sensitive ? c : std::tolower(c));
    char rhs = (cs == CaseSensitivity::Insensitive ? cursor.current() : std::tolower(cursor.current()));

    if (lhs == rhs) {
        cursor.advance(1);
        return true;
    }

    return false;
}

bool
match_until(char c, StreamCursor& cursor, CaseSensitivity cs) {
    return match_until( { c }, cursor, cs);
}

bool
match_until(std::initializer_list<char> chars, StreamCursor& cursor, CaseSensitivity cs) {
    if (cursor.eof())
        return false;

    auto find = [&](char val) {
        for (auto c: chars) {
            char lhs = cs == CaseSensitivity::Sensitive ? c : std::tolower(c);
            char rhs = cs == CaseSensitivity::Insensitive ? val : std::tolower(val);

            if (lhs == rhs) return true;
        }

        return false;
    };

    while (!cursor.eof()) {
        const char c = cursor.current();
        if (find(c)) return true;
        cursor.advance(1);
    }

    return false;
}

bool
match_double(double* val, StreamCursor &cursor) {
    // @Todo: strtod does not support a length argument
    char *end;
    *val = strtod(cursor.offset(), &end);
    if (end == cursor.offset())
        return false;

    cursor.advance(static_cast<ptrdiff_t>(end - cursor.offset()));
    return true;
}
