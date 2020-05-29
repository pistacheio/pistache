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

class StringLogger {
public:
  virtual void log(Level level, const std::string &message) = 0;
  virtual bool isEnabledFor(Level level) const = 0;

  virtual ~StringLogger() {}
};

class DefaultStringLogger : public StringLogger {
public:
  explicit DefaultStringLogger(Level level) : level_(level) {}
  ~DefaultStringLogger() override {}

  void log(Level level, const std::string &message) override;
  bool isEnabledFor(Level level) const override;
private:
  Level level_;
};

} // namespace Log
} // namespace Pistache

#ifndef PISTACHE_LOG_STRING_FATAL
#define PISTACHE_LOG_STRING_FATAL(logger, message) do {\
  if (logger && logger->isEnabledFor(::Pistache::Log::Level::FATAL)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::FATAL, oss_.str()); \
  } \
} while (0)
#endif

#ifndef PISTACHE_LOG_STRING_ERROR
#define PISTACHE_LOG_STRING_ERROR(logger, message) do {\
  if (logger && logger->isEnabledFor(::Pistache::Log::Level::ERROR)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::ERROR, oss_.str()); \
  } \
} while (0)
#endif

#ifndef PISTACHE_LOG_STRING_WARN
#define PISTACHE_LOG_STRING_WARN(logger, message) do {\
  if (logger && logger->isEnabledFor(::Pistache::Log::Level::WARN)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::WARN, oss_.str()); \
  } \
} while (0)
#endif

#ifndef PISTACHE_LOG_STRING_INFO
#define PISTACHE_LOG_STRING_INFO(logger, message) do {\
  if (logger && logger->isEnabledFor(::Pistache::Log::Level::INFO)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::INFO, oss_.str()); \
  } \
} while (0)
#endif

#ifndef PISTACHE_LOG_STRING_DEBUG
#define PISTACHE_LOG_STRING_DEBUG(logger, message) do {\
  if (logger && logger->isEnabledFor(::Pistache::Log::Level::DEBUG)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::DEBUG, oss_.str()); \
  } \
} while (0)
#endif

#ifndef PISTACHE_LOG_STRING_TRACE
#ifndef NDEBUG // Only enable trace logging in debug builds.
#define PISTACHE_LOG_STRING_TRACE(logger, message) do {\
  if (logger && logger->isEnabledFor(::Pistache::Log::Level::TRACE)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::TRACE, oss_.str()); \
  } \
} while (0)
#else
#define PISTACHE_LOG_STRING_TRACE(logger, message) do {\
  if (0) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::TRACE, oss_.str()); \
  } \
} while (0)
#endif
#endif

#ifndef PISTACHE_STRING_LOGGER_T
#define PISTACHE_STRING_LOGGER_T \
  std::shared_ptr<::Pistache::Log::StringLogger>
#endif

#ifndef PISTACHE_DEFAULT_STRING_LOGGER
#define PISTACHE_DEFAULT_STRING_LOGGER \
  std::make_shared<::Pistache::Log::DefaultStringLogger>(::Pistache::Log::Level::WARN)
#endif

#ifndef PISTACHE_NULL_STRING_LOGGER
#define PISTACHE_NULL_STRING_LOGGER \
  nullptr
#endif

