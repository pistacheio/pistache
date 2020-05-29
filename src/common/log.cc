/* log.cc
   Michael Ellison, 27 May 2020

   Implementation of the default logger
*/

#include <iostream>

#include <pistache/log.h>

namespace Pistache {
namespace Log {

void DefaultStringLogger::log(Level level, const std::string &message) {
  if (isEnabledFor(level)) {
    std::cerr << message << std::endl;
  }
}

bool DefaultStringLogger::isEnabledFor(Level level) const {
  return static_cast<int>(level) >= static_cast<int>(level_);
}

} // namespace Log
} // namespace Pistache


