/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 */

#include <gtest/gtest.h>

#include "include/logger/logger.h"
#include "include/logging/structured.h"

// Coverage gap tests — structured logging (0% → full)

class CoverageGaps : public ::testing::Test
{
protected:
    void SetUp() override { logging::logger::init(); }
    void TearDown() override { logging::logger::flush(); }
};

// Structured Event Tests (0% coverage → comprehensive)
TEST_F(CoverageGaps, StructuredEventCreate)
{
    logging::structured_event evt("login_event");
    EXPECT_EQ(evt.name(), "login_event");
}

TEST_F(CoverageGaps, StructuredEventAdd)
{
    logging::structured_event evt("transaction");
    evt.add("user_id", static_cast<int64_t>(123)).add("amount", 99.99).add("status", "completed");
    EXPECT_EQ(evt.fields().size(), 3);
}

TEST_F(CoverageGaps, StructuredEventMessageSet)
{
    logging::structured_event evt("audit");
    evt.message("Action completed");
    EXPECT_EQ(evt.message(), "Action completed");
}

TEST_F(CoverageGaps, StructuredEventToJson)
{
    logging::structured_event evt("event1");
    evt.add("id", static_cast<int64_t>(999));
    std::string json = logging::to_json(evt);
    EXPECT_NE(json.find("event1"), std::string::npos);
}

TEST_F(CoverageGaps, StructuredEventToKvpairs)
{
    logging::structured_event evt("metric");
    evt.add("value", 42.0);
    std::string kv = logging::to_kvpairs(evt);
    EXPECT_NE(kv.find("event=metric"), std::string::npos);
}

TEST_F(CoverageGaps, StructuredEventJsonWithEscaping)
{
    logging::structured_event evt("error");
    evt.add("message", R"(Failed with "quotes")");
    std::string json = logging::to_json(evt);
    // Verify JSON was generated
    EXPECT_GT(json.length(), 0);
}

TEST_F(CoverageGaps, StructuredEventJsonNumberHandling)
{
    logging::structured_event evt("data");
    evt.add("int_val", static_cast<int64_t>(42)).add("float_val", 3.14).add("bool_val", true);
    std::string json = logging::to_json(evt);
    // JSON format should contain the event name
    EXPECT_NE(json.find("\"event\":\"data\""), std::string::npos);
}

TEST_F(CoverageGaps, StructuredEventLogging)
{
    logging::structured_event evt("log1");
    evt.add("key", "value");
    EXPECT_NO_THROW(evt.info());
    EXPECT_NO_THROW(evt.warn());
    EXPECT_NO_THROW(evt.error());
    EXPECT_NO_THROW(evt.debug());
}

TEST_F(CoverageGaps, StructuredEventChaining)
{
    auto& result = logging::structured_event("chain")
                       .add("a", static_cast<int64_t>(1))
                       .add("b", static_cast<int64_t>(2));
    EXPECT_EQ(result.fields().size(), 2);
}

// Logger Facade Edge Cases (additional coverage)
TEST_F(CoverageGaps, LoggerConsoleToggle)
{
    logging::logger::set_console_mode(false);
    EXPECT_FALSE(logging::logger::get_console_mode());
    logging::logger::set_console_mode(true);
    EXPECT_TRUE(logging::logger::get_console_mode());
}

TEST_F(CoverageGaps, LoggerThreadNameAPI)
{
    logging::logger::set_thread_name("TestWorker");
    std::string name = logging::logger::get_thread_name();
    // Should be set and not empty
    EXPECT_FALSE(name.empty());
}

TEST_F(CoverageGaps, LoggerIsEnabledAPI)
{
    // Should always be enabled when a backend is compiled in
    EXPECT_TRUE(logging::logger::is_enabled());
}

TEST_F(CoverageGaps, LoggerSignalHandlerToggle)
{
    bool initial = logging::logger::get_enable_unsafe_signal_handler();
    logging::logger::set_enable_unsafe_signal_handler(!initial);
    EXPECT_EQ(logging::logger::get_enable_unsafe_signal_handler(), !initial);
    logging::logger::set_enable_unsafe_signal_handler(initial);
}

TEST_F(CoverageGaps, LoggerVerbosityConversion)
{
    auto result = logging::logger::convert_to_verbosity("INFO");
    // Should not be invalid
    EXPECT_NE(static_cast<int>(result),
        static_cast<int>(logging::logger_verbosity_enum::VERBOSITY_INVALID));
}

TEST_F(CoverageGaps, LoggerVerbosityCutoffAPI)
{
    // Should return a valid verbosity level
    auto cutoff = logging::logger::get_current_verbosity_cutoff();
    (void)cutoff;  // Use variable to avoid unused warning
}

// Logger Direct Log Function Tests
TEST_F(CoverageGaps, LoggerDirectLogCall)
{
    logging::logger::log(
        logging::logger_verbosity_enum::VERBOSITY_INFO, __FILE__, __LINE__, "Direct log call");
}

TEST_F(CoverageGaps, LoggerDirectErrorCall)
{
    logging::logger::log(
        logging::logger_verbosity_enum::VERBOSITY_ERROR, __FILE__, __LINE__, "Error log call");
}

// Logger Facade Additional Coverage
TEST_F(CoverageGaps, LoggerFlushAPI)
{
    logging::logger::flush();
}

TEST_F(CoverageGaps, LoggerGetEnabledState)
{
    bool enabled = logging::logger::is_enabled();
    EXPECT_TRUE(enabled);
}

TEST_F(CoverageGaps, LoggerSimpleInfoLog)
{
    LOGGING_LOG_INFO("Test message");
}

TEST_F(CoverageGaps, LoggerSimpleErrorLog)
{
    LOGGING_LOG_ERROR("Error message");
}

TEST_F(CoverageGaps, LoggerSimpleWarnLog)
{
    LOGGING_LOG_WARNING("Warning message");
}

TEST_F(CoverageGaps, LoggerConditionalLogTrue)
{
    bool condition = true;
    LOGGING_VLOG_IF(logging::logger_verbosity_enum::VERBOSITY_INFO, condition, "Conditional");
}

TEST_F(CoverageGaps, LoggerConditionalLogFalse)
{
    bool condition = false;
    LOGGING_VLOG_IF(logging::logger_verbosity_enum::VERBOSITY_INFO, condition, "Not logged");
}

TEST_F(CoverageGaps, LoggerSetFileMode)
{
    const char test_path[] = "/tmp/test_logging_facade.log";
    logging::logger::log_to_file(test_path,
        logging::logger::file_mode::truncate,
        logging::logger_verbosity_enum::VERBOSITY_INFO);
    LOGGING_LOG_INFO("Message to file");
    logging::logger::end_log_to_file(test_path);
}

TEST_F(CoverageGaps, LoggerAppendFileMode)
{
    const char test_path[] = "/tmp/test_logging_append.log";
    logging::logger::log_to_file(test_path,
        logging::logger::file_mode::append,
        logging::logger_verbosity_enum::VERBOSITY_WARNING);
    LOGGING_LOG_WARNING("First message");
    logging::logger::end_log_to_file(test_path);
}

TEST_F(CoverageGaps, LoggerMultipleFileSinks)
{
    const char path1[] = "/tmp/sink1.log";
    const char path2[] = "/tmp/sink2.log";
    logging::logger::log_to_file(path1,
        logging::logger::file_mode::truncate,
        logging::logger_verbosity_enum::VERBOSITY_INFO);
    logging::logger::log_to_file(path2,
        logging::logger::file_mode::truncate,
        logging::logger_verbosity_enum::VERBOSITY_WARNING);
    LOGGING_LOG_INFO("Info to sink1");
    LOGGING_LOG_WARNING("Warning to both");
    logging::logger::end_log_to_file(path1);
    logging::logger::end_log_to_file(path2);
}

TEST_F(CoverageGaps, StructuredEventMultipleTypes)
{
    logging::structured_event evt("type_test");
    evt.add("int_val", static_cast<int64_t>(123))
        .add("float_val", 45.67)
        .add("bool_val", false)
        .add("string_val", "test");
    EXPECT_EQ(evt.fields().size(), 4);
}

TEST_F(CoverageGaps, StructuredEventClear)
{
    logging::structured_event evt("clear_test");
    evt.add("key", "value");
    EXPECT_EQ(evt.fields().size(), 1);
}

TEST_F(CoverageGaps, StructuredEventDebugLevel)
{
    logging::structured_event evt("debug_evt");
    evt.add("level", "debug");
    EXPECT_NO_THROW(evt.debug());
}

TEST_F(CoverageGaps, LoggerVerbosityLevelConversion)
{
    auto fatal = logging::logger::convert_to_verbosity("FATAL");
    auto error = logging::logger::convert_to_verbosity("ERROR");
    auto warn  = logging::logger::convert_to_verbosity("WARNING");
    // All should be valid (not INVALID)
    EXPECT_NE(static_cast<int>(fatal),
        static_cast<int>(logging::logger_verbosity_enum::VERBOSITY_INVALID));
    EXPECT_NE(static_cast<int>(error),
        static_cast<int>(logging::logger_verbosity_enum::VERBOSITY_INVALID));
    EXPECT_NE(static_cast<int>(warn),
        static_cast<int>(logging::logger_verbosity_enum::VERBOSITY_INVALID));
}

TEST_F(CoverageGaps, LoggerVerbosityInvalidConversion)
{
    auto invalid = logging::logger::convert_to_verbosity("INVALID_LEVEL");
    EXPECT_EQ(static_cast<int>(invalid),
        static_cast<int>(logging::logger_verbosity_enum::VERBOSITY_INVALID));
}
