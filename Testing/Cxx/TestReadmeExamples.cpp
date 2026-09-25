#include <gtest/gtest.h>

#include "include/logger/config.h"
#include "include/logger/logger.h"
#include "include/logger/structured.h"

// Test: README API Examples Compile
// This test validates that code examples from README.md can be compiled.
// It tests the availability of key API headers and types referenced in documentation.

// Test 1: Basic logging API headers compile
TEST(ReadmeExamples, HeadersCompile)
{
    // Include paths resolve correctly
    // (This test just verifies includes work - the actual includes are at file level)
    EXPECT_TRUE(true);
}

// Test 2: logger_verbosity_enum exists and is usable
TEST(ReadmeExamples, VerbosityEnumExists)
{
    logging::logger_verbosity_enum lv = logging::logger_verbosity_enum::VERBOSITY_INFO;
    EXPECT_EQ(static_cast<int>(lv), 0);

    lv = logging::logger_verbosity_enum::VERBOSITY_WARNING;
    EXPECT_EQ(static_cast<int>(lv), -1);

    lv = logging::logger_verbosity_enum::VERBOSITY_ERROR;
    EXPECT_EQ(static_cast<int>(lv), -2);
}

// Test 3: Config object can be created
TEST(ReadmeExamples, ConfigStructure)
{
    logging::config cfg{
        .level   = logging::logger_verbosity_enum::VERBOSITY_INFO,
        .console = true
    };

    EXPECT_EQ(cfg.level, logging::logger_verbosity_enum::VERBOSITY_INFO);
    EXPECT_TRUE(cfg.console);
}

// Test 4: Structured events can be created (header-only verification)
TEST(ReadmeExamples, StructuredEventStructure)
{
    // Structured events provide semantic logging with key-value pairs.
    // Usage pattern: logging::structured_event("event_name").add("key", value).info();
    logging::structured_event event("readme_example");
    event.add("key", "value");
    EXPECT_EQ(event.name(), "readme_example");
}

// Test 5: Legacy Message struct (from logger/logger.h)
TEST(ReadmeExamples, MessageStructure)
{
    logging::logger::Message msg;
    msg.verbosity = logging::logger_verbosity_enum::VERBOSITY_INFO;
    msg.filename  = "test.cpp";
    msg.line      = 42;
    msg.message   = "Test";

    EXPECT_EQ(msg.line, 42);
    EXPECT_EQ(static_cast<int>(msg.verbosity), 0);
}
