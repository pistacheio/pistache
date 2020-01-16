/* mime.cc
   Mathieu Stefani, 29 August 2015

   Implementation of MIME Type parsing
*/

#include <cstring>

#include <pistache/http.h>
#include <pistache/mime.h>

namespace Pistache {
namespace Http {
namespace Mime {

std::string Q::toString() const {
  if (val_ == 0)
    return "q=0";
  else if (val_ == 100)
    return "q=1";

  char buff[sizeof("q=0.99")];
  memset(buff, 0, sizeof buff);
  if (val_ % 10 == 0)
    snprintf(buff, sizeof buff, "q=%.1f", val_ / 100.0);
  else
    snprintf(buff, sizeof buff, "q=%.2f", val_ / 100.0);

  return std::string(buff);
}

MediaType MediaType::fromString(const std::string &str) {
  return fromRaw(str.c_str(), str.size());
}

MediaType MediaType::fromString(std::string &&str) {
  return fromRaw(str.c_str(), str.size());
}

MediaType MediaType::fromRaw(const char *str, size_t len) {
  MediaType res;

  res.parseRaw(str, len);
  return res;
}

MediaType MediaType::fromFile(const char *fileName) {
  const char *extensionOffset = nullptr;
  const char *p = fileName;
  while (*p) {
    if (*p == '.')
      extensionOffset = p;
    ++p;
  }

  if (!extensionOffset)
    return MediaType();

  ++extensionOffset;

  struct Extension {
    const char *const raw;
    Mime::Type top;
    Mime::Subtype sub;
  };

  // @Data: maybe one day try to export
  // http://www.iana.org/assignments/media-types/media-types.xhtml as an
  // item-list

  static constexpr Extension KnownExtensions[] = {
      {"jpg", Type::Image, Subtype::Jpeg},
      {"jpeg", Type::Image, Subtype::Jpeg},
      {"png", Type::Image, Subtype::Png},
      {"bmp", Type::Image, Subtype::Bmp},

      {"txt", Type::Text, Subtype::Plain},
      {"md", Type::Text, Subtype::Plain},

      {"bin", Type::Application, Subtype::OctetStream},
  };

  for (const auto &ext : KnownExtensions) {
    if (!strcmp(extensionOffset, ext.raw)) {
      return MediaType(ext.top, ext.sub);
    }
  }

  return MediaType();
}

void MediaType::parseRaw(const char *str, size_t len) {
  auto raise = [&](const char *str) {
    // TODO: eventually, we should throw a more generic exception
    // that could then be catched in lower stack frames to rethrow
    // an HttpError
    throw HttpError(Http::Code::Unsupported_Media_Type, str);
  };

  RawStreamBuf<char> buf(const_cast<char *>(str), len);
  StreamCursor cursor(&buf);

  raw_ = std::string(str, len);

  Mime::Type top = Type::None;

  // The reason we are using a do { } while (0); syntax construct here is to
  // emulate if / else-if. Since we are using items-list macros to compare the
  // strings, we want to avoid evaluating all the branches when one of them
  // evaluates to true.
  //
  // Instead, we break the loop when a branch evaluates to true so that we do
  // not evaluate all the subsequent ones.
  //
  // Watch out, this pattern is repeated throughout the function
  do {
#define TYPE(val, s)                                                           \
  if (match_string(s, sizeof s - 1, cursor, CaseSensitivity::Insensitive)) {   \
    top = Type::val;                                                           \
    break;                                                                     \
  }
    MIME_TYPES
#undef TYPE
    raise("Unknown Media Type");
  } while (0);

  top_ = top;

  if (!match_literal('/', cursor))
    raise("Malformed Media Type, expected a '/' after the top type");

  if (cursor.eof())
    raise("Malformed Media type, missing subtype");

  // Parse subtype
  Mime::Subtype sub;

  StreamCursor::Token subToken(cursor);

  if (match_raw("vnd.", 4, cursor)) {
    sub = Subtype::Vendor;
  } else {
    do {
#define SUB_TYPE(val, s)                                                       \
  if (match_string(s, sizeof s - 1, cursor, CaseSensitivity::Insensitive)) {   \
    sub = Subtype::val;                                                        \
    break;                                                                     \
  }
      MIME_SUBTYPES
#undef SUB_TYPE
      sub = Subtype::Ext;
    } while (0);
  }

  if (sub == Subtype::Ext || sub == Subtype::Vendor) {
    (void)match_until({';', '+'}, cursor);
    rawSubIndex.beg = subToken.start();
    rawSubIndex.end = subToken.end() - 1;
  }

  sub_ = sub;

  if (cursor.eof())
    return;

  // Parse suffix
  Mime::Suffix suffix = Suffix::None;
  if (match_literal('+', cursor)) {

    if (cursor.eof())
      raise("Malformed Media Type, expected suffix, got EOF");

    StreamCursor::Token suffixToken(cursor);

    do {
#define SUFFIX(val, s, _)                                                      \
  if (match_string(s, sizeof s - 1, cursor, CaseSensitivity::Insensitive)) {   \
    suffix = Suffix::val;                                                      \
    break;                                                                     \
  }
      MIME_SUFFIXES
#undef SUFFIX
      suffix = Suffix::Ext;
    } while (0);

    if (suffix == Suffix::Ext) {
      (void)match_until({';', '+'}, cursor);
      rawSuffixIndex.beg = suffixToken.start();
      rawSuffixIndex.end = suffixToken.end() - 1;
    }

    suffix_ = suffix;
  }

  // Parse parameters
  while (!cursor.eof()) {

    if (cursor.current() == ';' || cursor.current() == ' ') {
      int c;
      if ((c = cursor.next()) == StreamCursor::Eof || c == 0)
        raise("Malformed Media Type, expected parameter got EOF");
      cursor.advance(1);
    }

    else if (match_literal('q', cursor)) {

      if (cursor.eof())
        raise("Invalid quality factor");

      if (match_literal('=', cursor)) {
        double val;
        if (!match_double(&val, cursor))
          raise("Invalid quality factor");
        q_ = Some(Q::fromFloat(val));
      } else {
        raise("Missing quality factor");
      }
    } else {
      StreamCursor::Token keyToken(cursor);
      (void)match_until('=', cursor);

      int c;
      if (cursor.eof() || (c = cursor.next()) == StreamCursor::Eof || c == 0)
        raise("Unfinished Media Type parameter");

      std::string key = keyToken.text();
      cursor.advance(1);

      StreamCursor::Token valueToken(cursor);
      (void)match_until({' ', ';'}, cursor);
      params.insert(std::make_pair(std::move(key), valueToken.text()));
    }
  }
}

void MediaType::setQuality(Q quality) { q_ = Some(quality); }

Optional<std::string> MediaType::getParam(const std::string &name) const {
  auto it = params.find(name);
  if (it == std::end(params)) {
    return Optional<std::string>(None());
  }

  return Optional<std::string>(Some(it->second));
}

void MediaType::setParam(const std::string &name, std::string value) {
  params[name] = std::move(value);
}

std::string MediaType::toString() const {

  if (!raw_.empty())
    return raw_;

  auto topString = [](Mime::Type top) -> const char * {
    switch (top) {
#define TYPE(val, str)                                                         \
  case Mime::Type::val:                                                        \
    return str;
      MIME_TYPES
#undef TYPE
    default:
      return "";
    }
  };

  auto subString = [](Mime::Subtype sub) -> const char * {
    switch (sub) {
#define SUB_TYPE(val, str)                                                     \
  case Mime::Subtype::val:                                                     \
    return str;
      MIME_SUBTYPES
#undef TYPE
    default:
      return "";
    }
  };

  auto suffixString = [](Mime::Suffix suffix) -> const char * {
    switch (suffix) {
#define SUFFIX(val, str, _)                                                    \
  case Mime::Suffix::val:                                                      \
    return "+" str;
      MIME_SUFFIXES
#undef SUFFIX
    default:
      return "";
    }
  };

  std::string res;
  res.reserve(128);
  res += topString(top_);
  res += "/";
  res += subString(sub_);
  if (suffix_ != Suffix::None) {
    res += suffixString(suffix_);
  }

  optionally_do(q_, [&res](Q quality) {
    res += "; ";
    res += quality.toString();
  });

  for (const auto &param : params) {
    res += "; ";
    res += param.first + "=" + param.second;
  }

  return res;
}

bool MediaType::isValid() const {
  return top_ != Type::None && sub_ != Subtype::None;
}

} // namespace Mime
} // namespace Http
} // namespace Pistache
