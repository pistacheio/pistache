#include <utility>
#include <vector>

#include <pistache/log.h>

#include "gtest/gtest.h"

using namespace Pistache;

class TestLogHandler : public Log::LogHandler {
public:
  void log(Log::Level level, const std::string &message) {
    messages_.push_back(message);
    levels_.push_back(level);
  }

  bool isEnabledFor(Log::Level level) const override {
    return static_cast<int>(level) >= static_cast<int>(level_);
  }

  TestLogHandler(Log::Level level) : level_(level) {}

  Log::Level level_;

  std::vector<std::string> messages_;
  std::vector<Log::Level>  levels_;
};

TEST(logger_test, basic_log_handler) {
  auto logger_subclass = std::make_unique<TestLogHandler>(Log::Level::WARN);
  auto& actual_messages = logger_subclass->messages_;
  auto& actual_levels = logger_subclass->levels_;

  DECLARE_PISTACHE_LOGGER(logger) = std::move(logger_subclass);

  PISTACHE_LOG(FATAL, logger, "test_message_1_fatal");
  PISTACHE_LOG(ERROR, logger, "test_message_2_error");
  PISTACHE_LOG(WARN,  logger, "test_message_3_warn");
  PISTACHE_LOG(INFO,  logger, "test_message_4_info");
  PISTACHE_LOG(DEBUG, logger, "test_message_5_debug");
  PISTACHE_LOG(TRACE, logger, "test_message_6_trace");

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

TEST(logger_test, basic_log_handler_uninitialized) {
  DECLARE_PISTACHE_LOGGER(logger) = nullptr;

  PISTACHE_LOG(FATAL, logger, "test_message_1_fatal");
  PISTACHE_LOG(ERROR, logger, "test_message_2_error");
  PISTACHE_LOG(WARN,  logger, "test_message_3_warn");
  PISTACHE_LOG(INFO,  logger, "test_message_4_info");
  PISTACHE_LOG(DEBUG, logger, "test_message_5_debug");
  PISTACHE_LOG(TRACE, logger, "test_message_6_trace");

  // Expect no death from accessing the nullptr.
}
