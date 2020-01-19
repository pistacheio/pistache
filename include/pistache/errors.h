#pragma once

#include <stdexcept>

namespace Pistache {
namespace Tcp {

class SocketError : public std::runtime_error {
public:
  explicit SocketError(const char *what_arg) : std::runtime_error(what_arg) {}
};

class ServerError : public std::runtime_error {
public:
  explicit ServerError(const char *what_arg) : std::runtime_error(what_arg) {}
};

} // namespace Tcp
} // namespace Pistache