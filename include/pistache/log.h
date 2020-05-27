/* log.h
   Michael Ellison, 27 May 2020

   Logging API definitions
*/

#pragma once

#include <memory>
#include <sstream>

namespace Pistache {
namespace Log {

enum class Level {
  TRACE,
  DEBUG,
  INFO,
  WARN,
  ERROR,
  FATAL
};

class LogHandler {
public:
  virtual void log(Level level, const std::string &message) = 0;
  virtual bool isEnabledFor(Level level) const = 0;

  virtual ~LogHandler() {}
};

class DefaultLogHandler : public LogHandler {
public:
  explicit DefaultLogHandler(Level level) : level_(level) {}
  ~DefaultLogHandler() override {}

  void log(Level level, const std::string &message) override;
  bool isEnabledFor(Level level) const override;
private:
  Level level_;
};

} // namespace Log
} // namespace Pistache

#ifndef PISTACHE_LOG
#define PISTACHE_LOG(level, logger_ptr, message) do {\
  if (logger_ptr && logger_ptr->isEnabledFor(::Pistache::Log::Level::level)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger_ptr->log(::Pistache::Log::Level::level, oss_.str()); \
  } \
} while (0)
#endif

#ifndef DECLARE_PISTACHE_LOGGER
#define DECLARE_PISTACHE_LOGGER(logger_ptr) \
  std::unique_ptr<::Pistache::Log::LogHandler> logger_ptr
#endif

