/* stream.h
   Mathieu Stefani, 05 September 2015
   
   A set of classes to control input over a sequence of bytes
*/

#pragma once

#include <pistache/os.h>

#include <cstddef>
#include <stdexcept>
#include <cstring>
#include <streambuf>
#include <vector>
#include <limits>
#include <iostream>
#include <string>


namespace Pistache {

static constexpr char CR = 0xD;
static constexpr char LF = 0xA;

template<typename CharT = char>
class StreamBuf : public std::basic_streambuf<CharT> {
public:
    typedef std::basic_streambuf<CharT> Base;
    typedef typename Base::traits_type traits_type;

    void setArea(char* begin, char *current, char *end) {
        this->setg(begin, current, end);
    }

    CharT *begptr() const {
        return this->eback();
    }

    CharT* curptr() const {
        return this->gptr();
    }

    CharT* endptr() const {
        return this->egptr();
    }

    size_t position() const {
        return this->gptr() - this->eback();
    }

    void reset() {
        this->setg(nullptr, nullptr, nullptr);
    }

    typename Base::int_type snext() const {
        if (this->gptr() == this->egptr()) {
            return traits_type::eof();
        }

        const CharT* gptr = this->gptr();
        return *(gptr + 1);
    }
};

template<typename CharT = char>
class RawStreamBuf : public StreamBuf<CharT> {
public:
    typedef StreamBuf<CharT> Base;

    RawStreamBuf(char* begin, char* end) {
        Base::setg(begin, begin, end);
    }
    RawStreamBuf(char* begin, size_t len) {
        Base::setg(begin, begin, begin + len);
    }
};

// Make the buffer dynamic
template<typename CharT = char>
class ArrayStreamBuf : public StreamBuf<CharT> {
public:
    typedef StreamBuf<CharT> Base;
    static size_t maxSize;

    ArrayStreamBuf()
      : StreamBuf<CharT>()
      , bytes()
    {
        bytes.clear();
        Base::setg(bytes.data(), bytes.data(), bytes.data() + bytes.size());
    }

    template<size_t M>
    ArrayStreamBuf(char (&arr)[M]) {
        bytes.clear();
        std::copy(arr, arr + M, std::back_inserter(bytes));
        Base::setg(bytes.data(), bytes.data(), bytes.data() + bytes.size());
    }

    bool feed(const char* data, size_t len) {
        if (bytes.size() + len > maxSize) { return false; }
        // persist current offset
        size_t readOffset = static_cast<size_t>(this->gptr() - this->eback());
        std::copy(data, data + len, std::back_inserter(bytes));
        Base::setg(bytes.data()
                  , bytes.data() + readOffset
                  , bytes.data() + bytes.size());
        return true;
    }

    void reset() {
        std::vector<CharT> nbytes;
        bytes.swap(nbytes);
        Base::setg(bytes.data(), bytes.data(), bytes.data() + bytes.size());
    }

private:
    std::vector<CharT> bytes;
};

template<typename CharT>
size_t ArrayStreamBuf<CharT>::maxSize = Const::DefaultMaxPayload;

struct RawBuffer
{
    RawBuffer();
    RawBuffer(std::string data, size_t length, bool isDetached = false);
    RawBuffer(const char* data, size_t length, bool isDetached = false);

    RawBuffer detach(size_t fromIndex);
    const std::string& data() const;
    size_t size() const;
    bool isDetached() const;
private:
    std::string data_;
    size_t length_;
    bool isDetached_;
};

struct FileBuffer
{
    explicit FileBuffer(const std::string& fileName);

    Fd fd() const;
    size_t size() const;

private:
    std::string fileName_;
    Fd fd_;
    size_t size_;
};

class DynamicStreamBuf : public StreamBuf<char> {
public:

    typedef StreamBuf<char> Base;
    typedef typename Base::traits_type traits_type;
    typedef typename Base::int_type int_type;

    DynamicStreamBuf(
            size_t size,
            size_t maxSize = std::numeric_limits<uint32_t>::max())
        : maxSize_(maxSize)
        , data_()
    {
        reserve(size);
    }

    DynamicStreamBuf(const DynamicStreamBuf& other) = delete;
    DynamicStreamBuf& operator=(const DynamicStreamBuf& other) = delete;

    DynamicStreamBuf(DynamicStreamBuf&& other)
       : maxSize_(other.maxSize_)
       , data_(std::move(other.data_)) {
           setp(other.pptr(), other.epptr());
           other.setp(nullptr, nullptr);
    }

    DynamicStreamBuf& operator=(DynamicStreamBuf&& other) {
        maxSize_ = other.maxSize_;
        data_ = std::move(other.data_);
        setp(other.pptr(), other.epptr());
        other.setp(nullptr, nullptr);
        return *this;
    }

    RawBuffer buffer() const {
        return RawBuffer((const char*) data_.data(), pptr() - &data_[0]);
    }

    void clear() {
        data_.clear();
        this->setp(&data_[0], &data_[0] + data_.capacity());
    }

  protected:
    int_type overflow(int_type ch);

private:
    void reserve(size_t size);

    size_t maxSize_;
    std::vector<char> data_;
};

class StreamCursor {
public:
    StreamCursor(StreamBuf<char>* _buf, size_t initialPos = 0)
        : buf(_buf)
    {
        advance(initialPos);
    }

    static constexpr int Eof = -1;

    struct Token {
        Token(StreamCursor& _cursor)
            : cursor(_cursor)
            , position(cursor.buf->position())
            , eback(cursor.buf->begptr())
            , gptr(cursor.buf->curptr())
            , egptr(cursor.buf->endptr())
        { }

        size_t start() const { return position; }

        size_t end() const {
            return cursor.buf->position();
        }

        size_t size() const {
            return end() - start();
        }

        std::string text() const {
            return std::string(gptr, size());
        }

        const char* rawText() const {
            return gptr;
        }

      private:
        StreamCursor& cursor;
        size_t position;
        char *eback;
        char *gptr;
        char *egptr;
    };

    struct Revert {
        Revert(StreamCursor& _cursor)
            : cursor(_cursor)
            , eback(cursor.buf->begptr())
            , gptr(cursor.buf->curptr())
            , egptr(cursor.buf->endptr())
            , active(true)
        { }

        Revert(const Revert&) = delete;
        Revert& operator=(const Revert&) = delete;

        ~Revert() {
            if (active)
                revert();
        }

        void revert() {
            cursor.buf->setArea(eback, gptr, egptr);
        }

        void ignore() {
            active = false;
        }

    private:
        StreamCursor& cursor;
        char *eback;
        char *gptr;
        char *egptr;
        bool active;
    };

    bool advance(size_t count);
    operator size_t() const { return buf->position(); }

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

public:
    StreamBuf<char>* buf;
};


enum class CaseSensitivity {
    Sensitive, Insensitive
};

bool match_raw(const void* buf, size_t len, StreamCursor& cursor);
bool match_string(const char *str, size_t len, StreamCursor& cursor,
        CaseSensitivity cs = CaseSensitivity::Insensitive);
bool match_literal(char c, StreamCursor& cursor, CaseSensitivity cs = CaseSensitivity::Insensitive);
bool match_until(char c, StreamCursor& cursor, CaseSensitivity cs = CaseSensitivity::Insensitive);
bool match_until(std::initializer_list<char> chars, StreamCursor& cursor, CaseSensitivity cs = CaseSensitivity::Insensitive);
bool match_double(double* val, StreamCursor& cursor);

void skip_whitespaces(StreamCursor& cursor);

} // namespace Pistache
