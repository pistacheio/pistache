#pragma once

#define CUSTOM_STRING_VIEW 1

#if __cplusplus >= 201703L
#if defined(__has_include)
#if __has_include(<string_view>)
#undef CUSTOM_STRING_VIEW
#include <string_view>
#elif __has_include(<experimental/string_view>)
#undef CUSTOM_STRING_VIEW
#include <experimental/string_view>
namespace std {
using string_view = experimental::string_view;
}
#endif
#endif
#endif

#ifdef CUSTOM_STRING_VIEW

#include <endian.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace std {

class string_view {
public:
  using size_type = std::size_t;

  static constexpr size_type npos = size_type(-1);

  constexpr string_view() noexcept : data_(nullptr), size_(0) {}

  constexpr string_view(const string_view &other) noexcept = default;

  constexpr string_view(const char *s, size_type count)
      : data_(s), size_(count) {}

  explicit string_view(const char *s) : data_(s), size_(strlen(s)) {}

  string_view substr(size_type pos, size_type count = npos) const {
    if (pos > size_) {
      throw std::out_of_range("Out of range.");
    }
    size_type rcount = std::min(count, size_ - pos);
    return {data_ + pos, rcount};
  }

  constexpr size_type size() const noexcept { return size_; }

  constexpr size_type length() const noexcept { return size_; }

  constexpr char operator[](size_type pos) const { return data_[pos]; }

  constexpr const char *data() const noexcept { return data_; }

  bool operator==(const string_view &rhs) const {
    return (!std::lexicographical_compare(data_, data_ + size_, rhs.data_,
                                          rhs.data_ + rhs.size_) &&
            !std::lexicographical_compare(rhs.data_, rhs.data_ + rhs.size_,
                                          data_, data_ + size_));
  }

  string_view &operator=(const string_view &view) noexcept = default;

  size_type find(string_view v, size_type pos = 0) const noexcept {
    if (size_ < pos)
      return npos;

    if ((size_ - pos) < v.size_)
      return npos;

    for (; pos <= (size_ - v.size_); ++pos) {
      bool found = true;
      for (size_type i = 0; i < v.size_; ++i) {
        if (data_[pos + i] != v.data_[i]) {
          found = false;
          break;
        }
      }
      if (found) {
        return pos;
      }
    }

    return npos;
  }

  size_type find(char ch, size_type pos = 0) const noexcept {
    return find(string_view(&ch, 1), pos);
  }

  size_type find(const char *s, size_type pos, size_type count) const {
    return find(string_view(s, count), pos);
  }

  size_type find(const char *s, size_type pos = 0) const {
    return find(string_view(s), pos);
  }

  size_type rfind(string_view v, size_type pos = npos) const noexcept {
    if (v.size_ > size_)
      return npos;

    if (pos > size_)
      pos = size_;
    size_t start = size_ - v.size_;
    if (pos != npos)
      start = pos;
    for (size_t offset = 0; offset <= pos; ++offset, --start) {
      bool found = true;
      for (size_t j = 0; j < v.size_; ++j) {
        if (data_[start + j] != v.data_[j]) {
          found = false;
          break;
        }
      }
      if (found) {
        return start;
      }
    }

    return npos;
  }

  size_type rfind(char c, size_type pos = npos) const noexcept {
    return rfind(string_view(&c, 1), pos);
  }

  size_type rfind(const char *s, size_type pos, size_type count) const {
    return rfind(string_view(s, count), pos);
  }

  size_type rfind(const char *s, size_type pos = npos) const {
    return rfind(string_view(s), pos);
  }

  constexpr bool empty() const noexcept { return size_ == 0; }

private:
  const char *data_;
  size_type size_;
};

template <> struct hash<string_view> {
  //-----------------------------------------------------------------------------
  // MurmurHash3 was written by Austin Appleby, and is placed in the public
  // domain. The author hereby disclaims copyright to this source code.
private:
#ifdef __GNUC__
#define FORCE_INLINE __attribute__((always_inline)) inline
#else
#define FORCE_INLINE inline
#endif

  static FORCE_INLINE std::uint32_t Rotl32(const std::uint32_t x,
                                           const std::int8_t r) {
    return (x << r) | (x >> (32 - r));
  }

  static FORCE_INLINE std::uint32_t Getblock(const std::uint32_t *p,
                                             const int i) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return p[i];
#else
    std::uint32_t temp_number = p[i];
    uint8_t(&number)[4] = *reinterpret_cast<uint8_t(*)[4]>(&temp_number);
    std::swap(number[0], number[3]);
    std::swap(number[1], number[2]);
    return temp_number;
#endif
  }

  static FORCE_INLINE uint32_t fmix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;

    return h;
  }

public:
  string_view::size_type operator()(const string_view &str) const {
    const uint32_t len = static_cast<uint32_t>(str.length());
    const uint8_t *data = reinterpret_cast<const uint8_t *>(str.data());
    const int nblocks = static_cast<int>(len / 4);

    uint32_t h1 = 0; // seed

    const uint32_t c1 = 0xcc9e2d51U;
    const uint32_t c2 = 0x1b873593U;

    const uint32_t *blocks =
        reinterpret_cast<const uint32_t *>(data + nblocks * 4);

    for (auto i = -nblocks; i; ++i) {
      uint32_t k1 = Getblock(blocks, i);

      k1 *= c1;
      k1 = Rotl32(k1, 15);
      k1 *= c2;

      h1 ^= k1;
      h1 = Rotl32(h1, 13);
      h1 = h1 * 5 + 0xe6546b64;
    }

    const uint8_t *tail = data + nblocks * 4;

    uint32_t k1 = 0;

    switch (len & 3) {
    case 3:
      k1 ^= tail[2] << 16;
      /* fall through */
    case 2:
      k1 ^= tail[1] << 8;
      /* fall through */
    case 1:
      k1 ^= tail[0];
      k1 *= c1;
      k1 = Rotl32(k1, 15);
      k1 *= c2;
      h1 ^= k1;
    default:
      break;
    }

    h1 ^= len;

    h1 = fmix32(h1);
    return hash<int>()(h1);
  }

#undef FORCE_INLINE
};
} // namespace std

#endif
