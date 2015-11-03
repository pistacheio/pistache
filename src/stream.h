/* stream.h
   Mathieu Stefani, 05 September 2015
   
   A set of classes to control input over a sequence of bytes
*/

#pragma once

#include <cstddef>
#include <stdexcept>
#include <cstring>

static constexpr char CR = 0xD;
static constexpr char LF = 0xA;

class BasicStreamBuf {
public:
    friend class StreamCursor;

    size_t totalAvailable() const { return end - begin; }
    size_t breakAvailable() const { return brk - begin; }

protected:
    BasicStreamBuf() { }

    void setArea(const char *begin, const char *end, const char* brk);

private:
    const char *begin;
    const char *brk;
    const char *end;
};

class StreamBuf : public BasicStreamBuf {
public:
    StreamBuf(const char* begin, const char* end);
    StreamBuf(const char *begin, const char* end, const char* brk);
    StreamBuf(const char* begin, size_t len);
};

template<size_t N>
class ArrayStreamBuf : public BasicStreamBuf {
public:
    ArrayStreamBuf()
      : size(0)
    {
        memset(bytes, 0, N);
        setArea(bytes, bytes, bytes);
    }

    template<size_t M>
    ArrayStreamBuf(char (&arr)[M]) {
        static_assert(M <= N, "Source array exceeds maximum capacity");
        memcpy(bytes, arr, M);
        size = M;
        setArea(bytes, bytes + N, bytes + M);
    }

    bool feed(const char* data, size_t len) {
        if (size + len >= N) {
            return false;
        }

        memcpy(bytes + size, data, len);
        size += len;
        setArea(bytes, bytes + N, bytes + size);
        return true;
    }

    void reset() {
        memset(bytes, 0, N);
        size = 0;
        setArea(bytes, bytes, bytes);
    }

private:
    char bytes[N];
    size_t size;
};

class StreamCursor {
public:
    StreamCursor(const BasicStreamBuf& buf, size_t initialPos = 0)
        : buf(buf)
        , value(initialPos)
    { }

    static constexpr int Eof = -1;

    struct Token {
        Token(StreamCursor& cursor)
            : cursor(cursor)
            , pos(cursor.value)
        { }

        size_t start() const { return pos; }
        size_t end() const {
            return cursor.value;
        }

        size_t size() const {
            return cursor.value - pos;
        }

        std::string text() {
            const char *beg = cursor.offset(pos);
            return std::string(beg, size());
        }

        const char* rawText() const {
            return cursor.offset(pos);
        }

    private:
        StreamCursor& cursor;
        size_t pos;
    };

    struct Revert {
        Revert(StreamCursor& cursor)
            : cursor(cursor)
            , pos(cursor.value)
            , active(true)
        { }

        ~Revert() {
            if (active)
                cursor.value = pos;
        }

        void revert() {
            cursor.value = pos;
        }

        void ignore() {
            active = false;
        }

    private:
        StreamCursor& cursor;
        size_t pos;
        bool active;
    };

    bool advance(size_t count);
    operator size_t() const { return value; }

    bool eol() const;
    bool eof() const;
    int next() const;
    char current() const;

    const char* offset() const;
    const char* offset(size_t off) const;

    size_t diff(size_t other) const;
    size_t diff(const StreamCursor& other) const;

    size_t remaining() const;

    void reset();

private:
    const BasicStreamBuf& buf;
    size_t value;
};

enum class CaseSensitivity {
    Sensitive, Insensitive
};

bool match_raw(const void* buf, size_t len, StreamCursor& cursor);
bool match_literal(char c, StreamCursor& cursor, CaseSensitivity cs = CaseSensitivity::Insensitive);
bool match_until(char c, StreamCursor& cursor, CaseSensitivity cs = CaseSensitivity::Insensitive);
bool match_until(std::initializer_list<char> chars, StreamCursor& cursor, CaseSensitivity cs = CaseSensitivity::Insensitive);
bool match_double(double* val, StreamCursor& cursor);
