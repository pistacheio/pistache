#include <utility>
#include <vector>

#include <pistache/log.h>

#include "gtest/gtest.h"

using namespace Pistache;

class TestStringLogger : public Log::StringLogger {
public:
  void log(Log::Level level, const std::string &message) {
    messages_.push_back(message);
    levels_.push_back(level);
  }

  bool isEnabledFor(Log::Level level) const override {
    switch(level) {
      case Log::Level::FATAL:
      case Log::Level::ERROR:
      case Log::Level::WARN:
        return true;
      case Log::Level::INFO:
      case Log::Level::DEBUG:
      case Log::Level::TRACE:
      default:
        return false;
    }
  }

  TestStringLogger(Log::Level level) : level_(level) {}

  Log::Level level_;

  std::vector<std::string> messages_;
  std::vector<Log::Level>  levels_;
};

// Test that isEnabledFor is called when using the PISTACHE_LOG_STRING_* macros.
TEST(logger_test, macros_guard_by_level) {
  auto logger_subclass = std::make_shared<TestStringLogger>(Log::Level::WARN);
  auto& actual_messages = logger_subclass->messages_;
  auto& actual_levels = logger_subclass->levels_;

  std::shared_ptr<::Pistache::Log::StringLogger> logger = logger_subclass;

  PISTACHE_LOG_STRING_FATAL(logger, "test_message_1_fatal");
  PISTACHE_LOG_STRING_ERROR(logger, "test_message_2_error");
  PISTACHE_LOG_STRING_WARN(logger,  "test_message_3_warn");
  PISTACHE_LOG_STRING_INFO(logger,  "test_message_4_info");
  PISTACHE_LOG_STRING_DEBUG(logger, "test_message_5_debug");
  PISTACHE_LOG_STRING_TRACE(logger, "test_message_6_trace");

  std::vector<std::string> expected_messages;
  expected_messages.push_back("test_message_1_fatal");
  expected_messages.push_back("test_message_2_error");
  expected_messages.push_back("test_message_3_warn");

  std::vector<Log::Level> expected_levels;
  expected_levels.push_back(Log::Level::FATAL);
  expected_levels.push_back(Log::Level::ERROR);
  expected_levels.push_back(Log::Level::WARN);

  ASSERT_EQ(actual_messages, expected_messages);
  ASSERT_EQ(actual_levels, expected_levels);
}

// Test that the PISTACHE_LOG_STRING_* macros guard against accessing a null logger.
TEST(logger_test, macros_guard_null_logger) {
  PISTACHE_STRING_LOGGER_T logger = PISTACHE_NULL_STRING_LOGGER;

  PISTACHE_LOG_STRING_FATAL(logger, "test_message_1_fatal");
  PISTACHE_LOG_STRING_ERROR(logger, "test_message_2_error");
  PISTACHE_LOG_STRING_WARN(logger,  "test_message_3_warn");
  PISTACHE_LOG_STRING_INFO(logger,  "test_message_4_info");
  PISTACHE_LOG_STRING_DEBUG(logger, "test_message_5_debug");
  PISTACHE_LOG_STRING_TRACE(logger, "test_message_6_trace");

  // Expect no death from accessing the default logger.
}

// Test that the PISTACHE_LOG_STRING_* macros access a default logger.
TEST(logger_test, macros_access_default_logger) {
  PISTACHE_STRING_LOGGER_T logger = PISTACHE_DEFAULT_STRING_LOGGER;

  PISTACHE_LOG_STRING_FATAL(logger, "test_message_1_fatal");
  PISTACHE_LOG_STRING_ERROR(logger, "test_message_2_error");
  PISTACHE_LOG_STRING_WARN(logger,  "test_message_3_warn");
  PISTACHE_LOG_STRING_INFO(logger,  "test_message_4_info");
  PISTACHE_LOG_STRING_DEBUG(logger, "test_message_5_debug");
  PISTACHE_LOG_STRING_TRACE(logger, "test_message_6_trace");

  // Expect no death from using the default handler. The only output of the 
  // default logger is to stdout, so output cannot be confirmed by gtest.
}

