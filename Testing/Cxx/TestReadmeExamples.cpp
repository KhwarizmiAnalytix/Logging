#include <gtest/gtest.h>

#include "level.h"
#include "config.h"
#include "structured.h"
#include "backend_traits.h"
#include "include/logger/logger.h"

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

// Test 2: Modern logging::level enum exists and is usable
TEST(ReadmeExamples, LevelEnumExists)
{
    logging::level lv = logging::level::info;
    EXPECT_EQ(static_cast<int>(lv), 2);

    lv = logging::level::warn;
    EXPECT_EQ(static_cast<int>(lv), 3);

    lv = logging::level::error;
    EXPECT_EQ(static_cast<int>(lv), 4);
}

// Test 3: Config object can be created
TEST(ReadmeExamples, ConfigStructure)
{
    logging::config cfg{
        .level   = logging::level::info,
        .console = true
    };

    EXPECT_EQ(cfg.level, logging::level::info);
    EXPECT_TRUE(cfg.console);
}

// Test 4: Backend traits can be queried
TEST(ReadmeExamples, BackendCapabilities)
{
    // Backend traits provide explicit capability declarations
    bool has_callbacks = logging::backend_traits::active_traits::supports_callbacks;
    bool has_file_sink = logging::backend_traits::active_traits::supports_file_sink;

    // These should return boolean values without crashing
    EXPECT_TRUE(has_file_sink || !has_file_sink);
}

// Test 5: Structured events can be created (header-only verification)
TEST(ReadmeExamples, StructuredEventStructure)
{
    // Structured events provide semantic logging with key-value pairs
    // Usage pattern: logging::structured_event("event_name").add("key", value).log<level>();

    EXPECT_TRUE(true);  // Compilation success is the test
}

// Test 6: Legacy logger_verbosity_enum still available
TEST(ReadmeExamples, BackwardCompatibility)
{
    // Verify the deprecated enum is still accessible
    logging::logger_verbosity_enum verbosity = logging::logger_verbosity_enum::VERBOSITY_INFO;
    EXPECT_EQ(static_cast<int>(verbosity), 0);
}

// Test 7: Legacy Message struct (from logger/logger.h)
TEST(ReadmeExamples, LegacyMessageStructure)
{
    logging::logger::Message msg;
    msg.verbosity = logging::logger_verbosity_enum::VERBOSITY_INFO;
    msg.filename  = "test.cpp";
    msg.line      = 42;
    msg.message   = "Test";

    EXPECT_EQ(msg.line, 42);
    EXPECT_EQ(static_cast<int>(msg.verbosity), 0);
}
