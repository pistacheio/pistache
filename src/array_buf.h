/* array_buf.h
   Mathieu Stefani, 26 August 2015
   
   An array backed streambuf
*/

#pragma once

#include <streambuf>

namespace Io {

enum class Init { Default, ZeroOut };

template<typename CharT>
class BasicOutArrayBuf : public std::basic_streambuf<CharT> {
public:

    BasicOutArrayBuf(CharT* begin, CharT* end, Init init = Init::Default) {
        if (init == Init::ZeroOut) {
            std::fill(begin, end, CharT());
        }

        this->setp(begin, end);
    }

    template<size_t N>
    BasicOutArrayBuf(char (&arr)[N], Init init = Init::Default) {
        if (init == Init::ZeroOut) {
            std::fill(arr, arr + N, CharT());
        }

        this->setp(arr, arr + N);
    }

    size_t len() const {
        return this->pptr() - this->pbase();
    }

};

typedef BasicOutArrayBuf<char> OutArrayBuf;

} // namespace IO
