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

#ifndef PISTACHE_LOG_FATAL
#define PISTACHE_LOG_FATAL(logger, message) do {\
  if (logger && logger->isEnabledFor(::Pistache::Log::Level::FATAL)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::FATAL, oss_.str()); \
  } \
} while (0)
#endif

#ifndef PISTACHE_LOG_ERROR
#define PISTACHE_LOG_ERROR(logger, message) do {\
  if (logger && logger->isEnabledFor(::Pistache::Log::Level::ERROR)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::ERROR, oss_.str()); \
  } \
} while (0)
#endif

#ifndef PISTACHE_LOG_WARN
#define PISTACHE_LOG_WARN(logger, message) do {\
  if (logger && logger->isEnabledFor(::Pistache::Log::Level::WARN)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::WARN, oss_.str()); \
  } \
} while (0)
#endif

#ifndef PISTACHE_LOG_INFO
#define PISTACHE_LOG_INFO(logger, message) do {\
  if (logger && logger->isEnabledFor(::Pistache::Log::Level::INFO)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::INFO, oss_.str()); \
  } \
} while (0)
#endif

#ifndef PISTACHE_LOG_DEBUG
#define PISTACHE_LOG_DEBUG(logger, message) do {\
  if (logger && logger->isEnabledFor(::Pistache::Log::Level::DEBUG)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::DEBUG, oss_.str()); \
  } \
} while (0)
#endif

#ifndef PISTACHE_LOG_TRACE
#ifndef NDEBUG // Only enable trace logging in debug builds.
#define PISTACHE_LOG_TRACE(logger, message) do {\
  if (logger && logger->isEnabledFor(::Pistache::Log::Level::TRACE)) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::TRACE, oss_.str()); \
  } \
} while (0)
#else
#define PISTACHE_LOG_TRACE(logger, message) do {\
  if (0) { \
    std::ostringstream oss_; \
    oss_ << message; \
    logger->log(::Pistache::Log::Level::TRACE, oss_.str()); \
  } \
} while (0)
#endif
#endif

#ifndef PISTACHE_LOGGER_T
#define PISTACHE_LOGGER_T \
  std::shared_ptr<::Pistache::Log::LogHandler>
#endif

#ifndef PISTACHE_DEFAULT_LOGGER
#define PISTACHE_DEFAULT_LOGGER \
  std::make_shared<::Pistache::Log::DefaultLogHandler>(::Pistache::Log::Level::WARN)
#endif

#ifndef PISTACHE_NULL_LOGGER
#define PISTACHE_NULL_LOGGER \
  nullptr
#endif

