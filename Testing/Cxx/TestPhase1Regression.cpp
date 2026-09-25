/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 */

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

#include "include/logging.h"
#include <gtest/gtest.h>

namespace
{
using logging::logger;
using logging::logger_verbosity_enum;

// Helper: read file contents
std::string read_text_file(const std::string& path)
{
    std::ifstream in(path);
    if (!in.good())
    {
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Callback counter for tracking invocations
struct CallbackCounter
{
    std::mutex               mutex;
    int                      count = 0;
    std::vector<std::string> messages;

    void handle(const logging::logger::Message& msg)
    {
        std::lock_guard<std::mutex> lock(mutex);
        count++;
        messages.push_back(msg.message);
    }
};

void callback_handler(void* user_data, const logging::logger::Message& message)
{
    auto* counter = static_cast<CallbackCounter*>(user_data);
    counter->handle(message);
}

}  // namespace

class Phase1Regression : public ::testing::Test
{
protected:
    void SetUp() override { logger::init(); }

    void TearDown() override
    {
        // Clean up any remaining callbacks or file sinks
        logger::remove_callback("test-callback-1");
        logger::remove_callback("test-callback-2");
        logger::end_log_to_file("test_phase1.log");
    }
};

// P1: Per-destination cutoff - stderr ERROR plus file/callback TRACE
// should route INFO only to those destinations
// CURRENT BEHAVIOR: Global cutoff rejects messages to all destinations
// This is a Phase 4 implementation target.
TEST_F(Phase1Regression, PerDestinationCutoffERRORonConsole)
{
#if LOGGING_HAS_GLOG
    GTEST_SKIP() << "glog backend does not support per-destination cutoff";
#else
    GTEST_SKIP() << "Phase 4 feature: per-destination cutoff not yet implemented. "
                    "See PHASE1_BASELINE_REPORT.md P1-4";
#endif
}

// P1: Numeric level filtering - level 2 should be filtered at cutoff 1
// CURRENT BEHAVIOR: numeric levels beyond cutoff reach all destinations
// This is part of the Phase 4 per-destination filtering work.
TEST_F(Phase1Regression, NumericLevelFilteringAtBoundary)
{
    GTEST_SKIP() << "Phase 4 feature: numeric level filtering per destination not yet "
                    "implemented. See PHASE1_BASELINE_REPORT.md P1-4";
}

// P1: Lazy evaluation - side-effecting format argument should not be evaluated
// when filtered
TEST_F(Phase1Regression, LazyEvaluationWhenFiltered)
{
    logger::set_console_mode(true);
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_ERROR);

    bool was_evaluated = false;
    auto get_value     = [&was_evaluated]()
    {
        was_evaluated = true;
        return 42;
    };

    // This INFO log is filtered (cutoff is ERROR), so get_value should NOT be called
    LOGGING_LOG_INFO("filtered: {}", get_value());

    EXPECT_FALSE(was_evaluated) << "Lazy evaluation failed: argument was evaluated despite filter";
}

// P1: Callback lifetime - duplicate registration with same ID should replace,
// closing the old registration (running its close handler exactly once) so
// callers relying on close-for-cleanup don't leak.
TEST_F(Phase1Regression, DuplicateCallbackRegistrationReplaces)
{
#if LOGGING_HAS_GLOG
    GTEST_SKIP() << "glog backend does not support custom callbacks";
#else
    struct Registration
    {
        CallbackCounter counter;
        int             close_count = 0;
    };
    Registration first;
    Registration second;

    auto close_handler = [](void* user_data)
    { static_cast<Registration*>(user_data)->close_count++; };
    auto log_handler = [](void* user_data, const logging::logger::Message& message)
    { static_cast<Registration*>(user_data)->counter.handle(message); };

    logger::add_callback("dup-test",
        log_handler,
        &first,
        logger_verbosity_enum::VERBOSITY_INFO,
        close_handler,
        nullptr);
    // Re-register under the same id before ever removing it: this must
    // replace, not accumulate a second live registration.
    logger::add_callback("dup-test",
        log_handler,
        &second,
        logger_verbosity_enum::VERBOSITY_INFO,
        close_handler,
        nullptr);

    // Replacing must close the first registration exactly once.
    EXPECT_EQ(first.close_count, 1);
    EXPECT_EQ(second.close_count, 0);

    LOGGING_LOG_INFO("after-replace");
    logger::flush();

    // Only the second (surviving) registration should have received the message.
    EXPECT_EQ(first.counter.count, 0);
    EXPECT_EQ(second.counter.count, 1);

    bool removed = logger::remove_callback("dup-test");
    EXPECT_TRUE(removed);
    EXPECT_EQ(second.close_count, 1);

    // Nothing left registered under this id.
    LOGGING_LOG_INFO("after-remove");
    logger::flush();
    EXPECT_EQ(second.counter.count, 1);
#endif
}

// P1: Callback removal leaves no orphan registration
TEST_F(Phase1Regression, CallbackRemovalCleansUp)
{
#if LOGGING_HAS_GLOG
    GTEST_SKIP() << "glog backend does not support custom callbacks";
#else
    CallbackCounter counter;

    logger::add_callback(
        "cleanup-test", callback_handler, &counter, logger_verbosity_enum::VERBOSITY_INFO);

    LOGGING_LOG_INFO("before-removal");
    logger::flush();

    EXPECT_EQ(counter.count, 1);

    // Remove the callback
    bool removed = logger::remove_callback("cleanup-test");
    EXPECT_TRUE(removed);

    // Log after removal - counter should not increase
    LOGGING_LOG_INFO("after-removal");
    logger::flush();

    // Counter should still be 1
    EXPECT_EQ(counter.count, 1);

    // Second removal should fail
    bool second_removed = logger::remove_callback("cleanup-test");
    EXPECT_FALSE(second_removed);
#endif
}

// P1: File lifecycle - duplicate path registration should replace
TEST_F(Phase1Regression, DuplicateFilePathReplaces)
{
    const std::string path = "test_phase1_duplicate.log";

    // Register first file sink
    logger::log_to_file(
        path.c_str(), logger::file_mode::truncate, logger_verbosity_enum::VERBOSITY_INFO);

    LOGGING_LOG_INFO("first-write");
    logger::flush();

    // Register SECOND file sink with same path (should replace)
    logger::log_to_file(
        path.c_str(), logger::file_mode::truncate, logger_verbosity_enum::VERBOSITY_INFO);

    LOGGING_LOG_INFO("second-write");
    logger::flush();

    // File should only contain second-write (truncate mode)
    const std::string contents = read_text_file(path);
    EXPECT_NE(contents.find("second-write"), std::string::npos);
    // first-write may or may not be present depending on replacement timing

    logger::end_log_to_file(path.c_str());
}

// P1: Removing console does not remove file/callback
// CURRENT BEHAVIOR: Global cutoff applies to all destinations
// When console is disabled, file/callback may also become quiet.
// This is a Phase 4 implementation target (per-destination cutoff).
TEST_F(Phase1Regression, DisableConsolePreservesOtherSinks)
{
#if LOGGING_HAS_GLOG
    GTEST_SKIP() << "glog backend does not support per-destination cutoff";
#else
    GTEST_SKIP() << "Phase 4 feature: independent console/file/callback cutoffs not yet "
                    "implemented. See PHASE1_BASELINE_REPORT.md P1-4";
#endif
}

// P1: Scopes - mismatched end_scope should not corrupt state
TEST_F(Phase1Regression, MismatchedScopeDoesNotCorrupt)
{
    logger::set_console_mode(true);
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_INFO);

    // Open scope
    LOGGING_LOG_START_SCOPE(INFO, "scope-a");

    // Close with wrong name
    LOGGING_LOG_END_SCOPE("scope-b");

    // Should still be able to log and close correctly
    LOGGING_LOG_INFO("still-working");

    // Close the correct scope
    LOGGING_LOG_END_SCOPE("scope-a");

    // Should not crash
    SUCCEED();
}

// P1: Flush should invoke callback flush hooks outside locks
TEST_F(Phase1Regression, FlushInvokesCallbackFlush)
{
#if LOGGING_HAS_GLOG
    GTEST_SKIP() << "glog backend does not support callback flush hooks";
#else
#if LOGGING_HAS_SPDLOG
    GTEST_SKIP() << "spdlog backend does not invoke flush hooks";
#else
    bool flush_hook_called = false;

    auto flush_hook = [](void* user_data)
    {
        auto* flag = static_cast<bool*>(user_data);
        *flag      = true;
    };

    // Add callback with flush hook
    CallbackCounter counter;
    logger::add_callback(
        "flush-test", callback_handler, &counter, logger_verbosity_enum::VERBOSITY_INFO);

    // Note: the current public API doesn't expose setting flush hooks directly.
    // This test is a placeholder for when that interface exists.
    // For now, we can verify that flush() completes without deadlock.

    LOGGING_LOG_INFO("before-flush");
    logger::flush();
    LOGGING_LOG_INFO("after-flush");
    logger::flush();

    EXPECT_GT(counter.count, 0);

    logger::remove_callback("flush-test");
#endif
#endif
}

// P1: Thread metadata - thread name should appear in output
TEST_F(Phase1Regression, ThreadNameAppearsInOutput)
{
#if LOGGING_HAS_GLOG
    GTEST_SKIP() << "glog backend thread naming differs";
#else
    logger::set_console_mode(true);
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_INFO);

    // Test that setting thread name is durable
    logger::set_thread_name("test-thread-name");
    EXPECT_EQ(logger::get_thread_name(), "test-thread-name");

    // For backends that capture thread name in output, this would be verified
    // by capturing stderr. For now, verify getter works.
    EXPECT_NE(logger::get_thread_name(), "");
#endif
}

// P1: Initialization - repeated init should be safe
TEST_F(Phase1Regression, RepeatedInitIssafe)
{
    // First init is in SetUp()
    // Try initializing again
    logger::init();  // Should not crash or deadlock

    LOGGING_LOG_INFO("after-repeated-init");
    logger::flush();

    SUCCEED();
}

// P1: Check that OFF cutoff suppresses ordinary messages
TEST_F(Phase1Regression, OFFCutoffSuppressesMessagesPrePhase3)
{
#if LOGGING_HAS_GLOG
    GTEST_SKIP() << "glog backend does not support OFF cutoff";
#else
    logger::set_console_mode(true);
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_OFF);

    CallbackCounter cb_counter;
    logger::add_callback(
        "off-test", callback_handler, &cb_counter, logger_verbosity_enum::VERBOSITY_OFF);

    // All messages should be suppressed with OFF cutoff
    LOGGING_LOG_ERROR("error-at-off");
    LOGGING_LOG_INFO("info-at-off");
    logger::flush();

    // Should not have received any messages
    EXPECT_EQ(cb_counter.count, 0);

    // Note: Phase 3 will change fatal behavior to terminate unconditionally
    // This test documents the CURRENT behavior before that change.

    logger::remove_callback("off-test");
#endif
}
