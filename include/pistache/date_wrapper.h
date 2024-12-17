/*
 * SPDX-FileCopyrightText: 2024 George Sedov
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#if PISTACHE_USE_STD_CHRONO
#include <chrono>
#include <format>
namespace date
{
  using namespace std::chrono;

  /**
   * special case for the to_stream function, which was not introduced
   * in the standard, as the functionality was already a part of std::format
   */
  template <class CharT, class Traits, typename... Args>
  std::basic_ostream<CharT, Traits>&
  to_stream(std::basic_ostream<CharT, Traits>& os, const std::format_string<Args...>& fmt, Args&&... args)
  {
    os << std::format(fmt, std::forward<Args>(args)...);
    return os;
  }
}
#else
#include <date/date.h>
#endif