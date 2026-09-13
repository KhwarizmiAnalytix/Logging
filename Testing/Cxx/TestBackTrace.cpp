#include <string>
#include <vector>

#include "LoggingTest.h"
#include "back_trace.h"
#include "logger/logger.h"

namespace logging
{
namespace
{
// Helper functions to create a deeper call stack
void level_3()
{
    // Capture stack trace at this level
    auto trace = logging::back_trace::print(0, 10, false);
    EXPECT_FALSE(trace.empty());
}

void level_2()
{
    level_3();
}

void level_1()
{
    level_2();
}

}  // namespace
}  // namespace logging
using namespace logging;
// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST(BackTrace, basic_print)
{
    auto trace = logging::back_trace::print();
    EXPECT_FALSE(trace.empty());
}

TEST(BackTrace, print_with_skip_frames)
{
    auto trace_no_skip = logging::back_trace::print(0, 5, false);
    auto trace_skip_2  = logging::back_trace::print(2, 5, false);

    EXPECT_FALSE(trace_no_skip.empty());
    EXPECT_FALSE(trace_skip_2.empty());
}

TEST(BackTrace, print_with_max_frames)
{
    auto trace_5  = logging::back_trace::print(0, 5, false);
    auto trace_10 = logging::back_trace::print(0, 10, false);

    EXPECT_FALSE(trace_5.empty());
    EXPECT_FALSE(trace_10.empty());
}

// ============================================================================
// Enhanced API Tests
// ============================================================================

TEST(BackTrace, capture_and_format)
{
    // Capture raw frames
    logging::backtrace_options options;
    options.frames_to_skip           = 0;
    options.maximum_number_of_frames = 10;
    options.skip_python_frames       = false;

    auto frames = logging::back_trace::capture(options);
    EXPECT_FALSE(frames.empty());

    // Verify frame structure
    for (const auto& frame : frames)
    {
        EXPECT_FALSE(frame.function_name.empty());
        EXPECT_NE(frame.return_address, nullptr);
    }

    // Format the captured frames
    auto formatted = logging::back_trace::format(frames, options);
    EXPECT_FALSE(formatted.empty());
}

TEST(BackTrace, compact_format)
{
    logging::backtrace_options options;
    options.frames_to_skip           = 0;
    options.maximum_number_of_frames = 5;
    options.compact_format           = true;
    options.include_addresses        = false;
    options.include_offsets          = false;

    auto trace = logging::back_trace::print(options);
    EXPECT_FALSE(trace.empty());

    // Compact format should contain " -> " separators
    EXPECT_NE(trace.find(" -> "), std::string::npos);
}

TEST(BackTrace, compact_helper)
{
    auto trace = logging::back_trace::compact(5);
    EXPECT_FALSE(trace.empty());

    // Should contain function call chain
    EXPECT_NE(trace.find(" -> "), std::string::npos);
}

TEST(BackTrace, detailed_format_with_options)
{
    logging::backtrace_options options;
    options.frames_to_skip           = 0;
    options.maximum_number_of_frames = 5;
    options.compact_format           = false;
    options.include_addresses        = true;
    options.include_offsets          = true;

    auto trace = logging::back_trace::print(options);
    EXPECT_FALSE(trace.empty());

    // Detailed format should contain "frame #" markers
    EXPECT_NE(trace.find("frame #"), std::string::npos);
}

TEST(BackTrace, detailed_format_without_addresses)
{
    logging::backtrace_options options;
    options.frames_to_skip           = 0;
    options.maximum_number_of_frames = 5;
    options.include_addresses        = false;
    options.include_offsets          = false;

    auto trace = logging::back_trace::print(options);
    EXPECT_FALSE(trace.empty());
}

// ============================================================================
// Call Stack Depth Tests
// ============================================================================

TEST(BackTrace, deep_call_stack)
{
    level_1();
}

// ============================================================================
// Platform Support Tests
// ============================================================================

TEST(BackTrace, is_supported)
{
    bool supported = logging::back_trace::is_supported();

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    // Should be supported on major platforms
    EXPECT_TRUE(supported);
#endif
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(BackTrace, empty_frames_format)
{
    std::vector<logging::stack_frame> empty_frames;
    auto                              formatted = logging::back_trace::format(empty_frames);

    EXPECT_FALSE(formatted.empty());
    EXPECT_NE(formatted.find("No stack trace available"), std::string::npos);
}

TEST(BackTrace, zero_max_frames)
{
    logging::backtrace_options options;
    options.maximum_number_of_frames = 0;

    auto frames = logging::back_trace::capture(options);
    // Should return empty or minimal frames - exact behavior is implementation dependent
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST(BackTrace, set_stack_trace_on_error)
{
    const bool previous = logging::back_trace::capture_on_error();
    logging::back_trace::set_stack_trace_on_error(0);
    EXPECT_FALSE(logging::back_trace::capture_on_error());
    logging::back_trace::set_stack_trace_on_error(1);
    EXPECT_TRUE(logging::back_trace::capture_on_error());
    logging::back_trace::set_stack_trace_on_error(previous ? 1 : 0);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(BackTrace, usage_in_logging)
{
    LOGGING_LOG_INFO("Error occurred at:\n{}", logging::back_trace::print(0, 5));
}

TEST(BackTrace, usage_in_compact_logging)
{
    // Test that compact backtrace can be used in logging
    LOGGING_LOG_INFO("Call chain: {}", logging::back_trace::compact(5));
}
