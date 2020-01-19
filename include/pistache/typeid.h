/* typeid.h
   Mathieu Stefani, 30 novembre 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.

   This header provides a TypeId type that holds an unique identifier
   for a given type. Basically equivalent to std::type_info except that
   it does not rely on RTTI. The identifier is determined at compile-time.

   Inspired by Rust's std::TypeId
*/

#pragma once

#include <functional>

namespace Pistache {

class TypeId {
public:
  template <typename T> static TypeId of() {
    static char const id_{};

    return TypeId(&id_);
  }

  operator size_t() const { return reinterpret_cast<size_t>(id_); }

private:
  typedef void const *Id;

  explicit TypeId(Id id) : id_(id) {}

  Id id_;
};

#define APPLY_OP(lhs, rhs, op)                                                 \
  static_cast<size_t>(lhs) op static_cast<size_t>(rhs);

inline bool operator==(const TypeId &lhs, const TypeId &rhs) {
  return APPLY_OP(lhs, rhs, ==);
}

inline bool operator!=(const TypeId &lhs, const TypeId &rhs) {
  return APPLY_OP(lhs, rhs, !=);
}

inline bool operator<(const TypeId &lhs, const TypeId &rhs) {
  return APPLY_OP(lhs, rhs, <);
}

#undef APPLY_OP

} // namespace Pistache

namespace std {
template <> struct hash<Pistache::TypeId> {
  size_t operator()(const Pistache::TypeId &id) {
    return static_cast<size_t>(id);
  }
};
} // namespace std
