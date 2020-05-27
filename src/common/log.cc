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

#ifndef PISTACHE_LOG
#define PISTACHE_LOG(level, logger, message) do {\
  if (logger->isEnabledFor(level)) { \
    std::ostringstream oss_; \
    logger->log(level, oss_.str(oss_ << message)); \
  } \
} while (0)
#endif

} // namespace Log
} // namespace Pistache


