/* log.cc
   Michael Ellison, 27 May 2020

   Implementation of the default logger
*/

#include <iostream>

#include <pistache/log.h>

namespace Pistache {
namespace Log {

void DefaultLogHandler::log(Level level, const std::string &message) {
  if (isEnabledFor(level)) {
    std::cerr << message << std::endl;
  }
}

bool DefaultLogHandler::isEnabledFor(Level level) const {
  return static_cast<int>(level) >= static_cast<int>(level_);
}

} // namespace Log
} // namespace Pistache


