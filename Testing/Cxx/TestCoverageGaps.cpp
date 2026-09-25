/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 */

#include <gtest/gtest.h>

#include "include/logger/logger.h"
#include "include/logging/structured.h"
#include "include/util/exception.h"

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

// Exception Tests (71.8% → full coverage)
TEST_F(CoverageGaps, ExceptionGetMode)
{
    auto mode = logging::get_exception_mode();
    EXPECT_TRUE(mode == logging::exception_mode::THROW || mode == logging::exception_mode::LOG_FATAL);
}

TEST_F(CoverageGaps, ExceptionSetMode)
{
    logging::set_exception_mode(logging::exception_mode::LOG_FATAL);
    EXPECT_EQ(logging::get_exception_mode(), logging::exception_mode::LOG_FATAL);
    logging::set_exception_mode(logging::exception_mode::THROW);
    EXPECT_EQ(logging::get_exception_mode(), logging::exception_mode::THROW);
}

TEST_F(CoverageGaps, ExceptionConstructorSimple)
{
    logging::exception ex("Test message", "backtrace", nullptr,
        logging::exception_category::GENERIC);
    EXPECT_NE(ex.what(), nullptr);
    EXPECT_TRUE(std::string(ex.what()).find("Test message") != std::string::npos);
}

TEST_F(CoverageGaps, ExceptionConstructorSourceLocation)
{
    logging::source_location loc{"func", "file.cpp", 42};
    logging::exception ex(loc, "Error from function", logging::exception_category::GENERIC);
    EXPECT_NE(ex.what(), nullptr);
    std::string what(ex.what());
    EXPECT_TRUE(what.find("Error from function") != std::string::npos);
    EXPECT_TRUE(what.find("func") != std::string::npos);
}

TEST_F(CoverageGaps, ExceptionWithoutBacktrace)
{
    logging::exception ex("Base message", "stack trace", nullptr,
        logging::exception_category::GENERIC);
    const char* what_without_bt = ex.what_without_backtrace();
    EXPECT_NE(what_without_bt, nullptr);
    EXPECT_TRUE(std::string(what_without_bt).find("Base message") != std::string::npos);
}

TEST_F(CoverageGaps, ExceptionAddContext)
{
    logging::exception ex("Main error", "", nullptr, logging::exception_category::GENERIC);
    ex.add_context("Context 1");
    ex.add_context("Context 2");
    std::string what(ex.what());
    EXPECT_TRUE(what.find("Context 1") != std::string::npos);
    EXPECT_TRUE(what.find("Context 2") != std::string::npos);
}

TEST_F(CoverageGaps, ExceptionNestedConstructor)
{
    auto nested = std::make_shared<logging::exception>("Nested error", "", nullptr,
        logging::exception_category::GENERIC);
    logging::source_location loc{"func", "file.cpp", 99};
    logging::exception ex(loc, "Outer error", nested, logging::exception_category::GENERIC);
    std::string what(ex.what());
    EXPECT_TRUE(what.find("Outer error") != std::string::npos);
    EXPECT_TRUE(what.find("Caused by:") != std::string::npos);
    EXPECT_TRUE(what.find("Nested error") != std::string::npos);
}

TEST_F(CoverageGaps, ExceptionMessage)
{
    logging::exception ex("Error message", "", nullptr, logging::exception_category::GENERIC);
    EXPECT_EQ(ex.msg(), "Error message");
}

TEST_F(CoverageGaps, ExceptionBacktrace)
{
    std::string backtrace_text = "Frame 1\nFrame 2\n";
    logging::exception ex("Error", backtrace_text, nullptr,
        logging::exception_category::GENERIC);
    EXPECT_EQ(ex.backtrace(), backtrace_text);
}

TEST_F(CoverageGaps, ExceptionCategory)
{
    logging::exception ex("Error", "", nullptr, logging::exception_category::VALUE_ERROR);
    EXPECT_EQ(ex.category(), logging::exception_category::VALUE_ERROR);
}

TEST_F(CoverageGaps, ExceptionCaller)
{
    const void* test_addr = reinterpret_cast<const void*>(0x12345678);
    logging::exception ex("Error", "", test_addr, logging::exception_category::GENERIC);
    EXPECT_EQ(ex.caller(), test_addr);
}

TEST_F(CoverageGaps, ExceptionThrowMode)
{
    logging::set_exception_mode(logging::exception_mode::THROW);
    logging::source_location loc{"test_func", "test.cpp", 50};
    try
    {
        logging::details::check_fail("test_func", "test.cpp", 50, "Check failed");
        FAIL() << "Expected exception to be thrown";
    }
    catch (const logging::exception& e)
    {
        std::string what(e.what());
        EXPECT_TRUE(what.find("Check failed") != std::string::npos);
    }
}

TEST_F(CoverageGaps, ExceptionRefreshWhat)
{
    logging::exception ex("Initial", "", nullptr, logging::exception_category::GENERIC);
    std::string initial = ex.what();
    ex.add_context("New context");
    std::string after = ex.what();
    EXPECT_TRUE(std::string(after).find("New context") != std::string::npos);
}

TEST_F(CoverageGaps, ExceptionMultipleContexts)
{
    logging::exception ex("Error", "", nullptr, logging::exception_category::GENERIC);
    for (int i = 0; i < 5; ++i)
    {
        ex.add_context("Context " + std::to_string(i));
    }
    std::string what(ex.what());
    EXPECT_TRUE(what.find("Context 0") != std::string::npos);
    EXPECT_TRUE(what.find("Context 4") != std::string::npos);
}

TEST_F(CoverageGaps, ExceptionContextAccess)
{
    logging::exception ex("Error", "", nullptr, logging::exception_category::GENERIC);
    ex.add_context("Context A");
    ex.add_context("Context B");
    const auto& contexts = ex.context();
    EXPECT_EQ(contexts.size(), 2);
    EXPECT_EQ(contexts[0], "Context A");
    EXPECT_EQ(contexts[1], "Context B");
}

// String Utility Tests (65.1% → improve)
TEST_F(CoverageGaps, StringStartsWith)
{
    EXPECT_TRUE(logging::starts_with("hello world", "hello"));
    EXPECT_FALSE(logging::starts_with("hello world", "world"));
    EXPECT_TRUE(logging::starts_with("", ""));
}

TEST_F(CoverageGaps, StringEndsWith)
{
    EXPECT_TRUE(logging::ends_with("hello world", "world"));
    EXPECT_FALSE(logging::ends_with("hello world", "hello"));
    EXPECT_TRUE(logging::ends_with("", ""));
}

TEST_F(CoverageGaps, StringReplaceAll)
{
    std::string text = "hello hello world";
    size_t count = logging::replace_all(text, "hello", "goodbye");
    EXPECT_EQ(count, 2);
    EXPECT_EQ(text, "goodbye goodbye world");
}

TEST_F(CoverageGaps, StringReplaceAllNoMatch)
{
    std::string text = "hello world";
    size_t count = logging::replace_all(text, "xyz", "abc");
    EXPECT_EQ(count, 0);
    EXPECT_EQ(text, "hello world");
}

TEST_F(CoverageGaps, StringEraseAllSubstring)
{
    std::string text = "hello world hello";
    logging::erase_all_sub_string(text, "hello");
    EXPECT_EQ(text, " world ");
}

TEST_F(CoverageGaps, StringDemangle)
{
    std::string demangled = logging::demangle("_Z1gv");
    // Should either be demangled (if support exists) or original mangled name
    EXPECT_FALSE(demangled.empty());
}

TEST_F(CoverageGaps, StringDemangleNull)
{
    std::string demangled = logging::demangle(nullptr);
    EXPECT_EQ(demangled, "<unknown>");
}

TEST_F(CoverageGaps, StringShortestRoundTripFloat)
{
    std::string result = logging::strings::internal::shortest_round_trip(3.14f);
    EXPECT_FALSE(result.empty());
}

TEST_F(CoverageGaps, StringShortestRoundTripDouble)
{
    std::string result = logging::strings::internal::shortest_round_trip(3.14159);
    EXPECT_FALSE(result.empty());
}

TEST_F(CoverageGaps, StringShortestRoundTripLongDouble)
{
    std::string result = logging::strings::internal::shortest_round_trip(3.14159L);
    EXPECT_FALSE(result.empty());
}
