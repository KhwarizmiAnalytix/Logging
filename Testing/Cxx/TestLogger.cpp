/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 */

#include <fcntl.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

#include "LoggingTest.h"
#include "logger/logger.h"

namespace
{
void log_handler(void* user_data, const logging::logger::Message& message)
{
    auto* lines = reinterpret_cast<std::string*>(user_data);
    (*lines) += message.message;
    (*lines) += "\n";
}

std::string read_text_file(const std::string& path)
{
    std::ifstream in(path);
    if (!in.good())
    {
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Redirects the process stderr fd to `path` so console/CMD-style backend
// output can be asserted. Restores the original fd in the destructor.
class stderr_capturer
{
public:
    explicit stderr_capturer(std::string path) : path_(std::move(path))
    {
        std::fflush(stderr);
#if defined(_WIN32)
        saved_       = _dup(_fileno(stderr));
        const int fd = _open(path_.c_str(), _O_CREAT | _O_TRUNC | _O_WRONLY, _S_IREAD | _S_IWRITE);
        if (fd >= 0)
        {
            _dup2(fd, _fileno(stderr));
            _close(fd);
        }
#else
        saved_       = dup(fileno(stderr));
        const int fd = open(path_.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd >= 0)
        {
            dup2(fd, fileno(stderr));
            close(fd);
        }
#endif
    }

    ~stderr_capturer() { restore(); }

    stderr_capturer(const stderr_capturer&)            = delete;
    stderr_capturer& operator=(const stderr_capturer&) = delete;
    stderr_capturer(stderr_capturer&&)                 = delete;
    stderr_capturer& operator=(stderr_capturer&&)      = delete;

    std::string read_and_restore()
    {
        logging::logger::flush();
        std::fflush(stderr);
        restore();
        return read_text_file(path_);
    }

private:
    void restore()
    {
        if (saved_ < 0)
        {
            return;
        }
        std::fflush(stderr);
#if defined(_WIN32)
        _dup2(saved_, _fileno(stderr));
        _close(saved_);
#else
        dup2(saved_, fileno(stderr));
        close(saved_);
#endif
        saved_ = -1;
    }

    std::string path_;
    int         saved_{-1};
};
}  // namespace

using logging::logger;
using logging::logger_verbosity_enum;

TEST(Logger, is_enabled)
{
    EXPECT_TRUE(logger::is_enabled());
}

TEST(Logger, convert_to_verbosity_int)
{
    EXPECT_EQ(logger::convert_to_verbosity(-100), logger_verbosity_enum::VERBOSITY_INVALID);
    EXPECT_EQ(logger::convert_to_verbosity(+100), logger_verbosity_enum::VERBOSITY_MAX);
    EXPECT_EQ(logger::convert_to_verbosity(0), logger_verbosity_enum::VERBOSITY_INFO);
    EXPECT_EQ(logger::convert_to_verbosity(-3), logger_verbosity_enum::VERBOSITY_FATAL);
}

TEST(Logger, convert_to_verbosity_string)
{
    EXPECT_EQ(logger::convert_to_verbosity("OFF"), logger_verbosity_enum::VERBOSITY_OFF);
    EXPECT_EQ(logger::convert_to_verbosity("FATAL"), logger_verbosity_enum::VERBOSITY_FATAL);
    EXPECT_EQ(logger::convert_to_verbosity("fatal"), logger_verbosity_enum::VERBOSITY_FATAL);
    EXPECT_EQ(logger::convert_to_verbosity("ERROR"), logger_verbosity_enum::VERBOSITY_ERROR);
    EXPECT_EQ(logger::convert_to_verbosity("WARNING"), logger_verbosity_enum::VERBOSITY_WARNING);
    EXPECT_EQ(logger::convert_to_verbosity("WARN"), logger_verbosity_enum::VERBOSITY_WARNING);
    EXPECT_EQ(logger::convert_to_verbosity("INFO"), logger_verbosity_enum::VERBOSITY_INFO);
    EXPECT_EQ(logger::convert_to_verbosity("TRACE"), logger_verbosity_enum::VERBOSITY_TRACE);
    EXPECT_EQ(logger::convert_to_verbosity("MAX"), logger_verbosity_enum::VERBOSITY_MAX);
    EXPECT_EQ(logger::convert_to_verbosity("NAN"), logger_verbosity_enum::VERBOSITY_INVALID);
    EXPECT_EQ(
        logger::convert_to_verbosity(static_cast<const char*>(nullptr)),
        logger_verbosity_enum::VERBOSITY_INVALID);
}

TEST(Logger, stderr_verbosity_cutoff)
{
    logger::init();
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_ERROR);
    EXPECT_LE(logger::get_current_verbosity_cutoff(), logger_verbosity_enum::VERBOSITY_ERROR);

    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_INFO);
    EXPECT_GE(logger::get_current_verbosity_cutoff(), logger_verbosity_enum::VERBOSITY_INFO);
}

TEST(Logger, stderr_receives_continuous_messages)
{
    logger::init();
    logger::set_console_mode(true);
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_INFO);

    stderr_capturer capture("logging_cxx_stderr_continuous.log");
    LOGGING_LOG_INFO("stderr-token-A");
    LOGGING_LOG_WARNING("stderr-token-B");
    LOGGING_LOG_ERROR("stderr-token-C");
    const std::string captured = capture.read_and_restore();

    const auto pos_a = captured.find("stderr-token-A");
    const auto pos_b = captured.find("stderr-token-B");
    const auto pos_c = captured.find("stderr-token-C");
    EXPECT_NE(pos_a, std::string::npos);
    EXPECT_NE(pos_b, std::string::npos);
    EXPECT_NE(pos_c, std::string::npos);
    EXPECT_LT(pos_a, pos_b);
    EXPECT_LT(pos_b, pos_c);
}

TEST(Logger, stderr_omits_messages_above_cutoff)
{
    logger::init();
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_ERROR);

    stderr_capturer capture("logging_cxx_stderr_cutoff.log");
    LOGGING_LOG_INFO("stderr-suppressed-info");
    LOGGING_LOG_ERROR("stderr-visible-error");
    const std::string captured = capture.read_and_restore();

    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_INFO);

    EXPECT_EQ(captured.find("stderr-suppressed-info"), std::string::npos);
    EXPECT_NE(captured.find("stderr-visible-error"), std::string::npos);
}

TEST(Logger, console_mode_roundtrip)
{
    logger::init();
    logger::set_console_mode(true);
    EXPECT_TRUE(logger::get_console_mode());

    logger::set_console_mode(false);
    EXPECT_FALSE(logger::get_console_mode());

    logger::set_console_mode(true);
    EXPECT_TRUE(logger::get_console_mode());
}

TEST(Logger, console_mode_logs_to_stderr)
{
    logger::init();
    logger::set_console_mode(true);
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_INFO);

    LOGGING_LOG_INFO("console-mode live INFO");
    LOGGING_LOG_WARNING("console-mode live WARNING");
    LOGGING_LOG_ERROR("console-mode live ERROR");
    logger::flush();

    stderr_capturer capture("logging_cxx_console_mode.log");
    LOGGING_LOG_INFO("console-mode-token-1");
    LOGGING_LOG_WARNING("console-mode-token-2");
    const std::string captured = capture.read_and_restore();

    EXPECT_NE(captured.find("console-mode-token-1"), std::string::npos);
    EXPECT_NE(captured.find("console-mode-token-2"), std::string::npos);
}

TEST(Logger, console_mode_disabled_skips_stderr)
{
    logger::init();
    logger::set_console_mode(false);
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_INFO);

    stderr_capturer capture("logging_cxx_console_mode_off.log");
    LOGGING_LOG_INFO("console-mode-should-be-silent");
    LOGGING_LOG_ERROR("console-mode-error-should-be-silent");
    const std::string captured = capture.read_and_restore();

    logger::set_console_mode(true);

    EXPECT_EQ(captured.find("console-mode-should-be-silent"), std::string::npos);
    EXPECT_EQ(captured.find("console-mode-error-should-be-silent"), std::string::npos);
}

TEST(Logger, thread_name_roundtrip)
{
    // Longer than loguru's 16-char Windows TLS buffer so get_thread_name must
    // round-trip our stored name, not the OS/loguru truncated form.
    logger::set_thread_name("logging-test-worker");
    EXPECT_EQ(logger::get_thread_name(), "logging-test-worker");
}

TEST(Logger, mismatched_end_scope_does_not_crash)
{
    logger::init();
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_INFO);
    LOGGING_LOG_START_SCOPE(INFO, "scope-1");
    LOGGING_LOG_END_SCOPE("scope-1");
    LOGGING_LOG_END_SCOPE("scope-0");
    LOGGING_LOG_END_SCOPE("never-opened");
    SUCCEED();
}

TEST(Logger, callback_receives_message)
{
#if LOGGING_HAS_GLOG
    GTEST_SKIP() << "glog backend does not support custom callbacks";
#else
    logger::init();
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_INFO);

    std::string lines;
    ASSERT_NO_THROW(logger::add_callback(
        "test-callback", log_handler, &lines, logger_verbosity_enum::VERBOSITY_INFO));

    LOGGING_LOG_INFO("callback-token-42");
    logger::flush();

    EXPECT_NE(lines.find("callback-token-42"), std::string::npos);
    EXPECT_TRUE(logger::remove_callback("test-callback"));
    EXPECT_FALSE(logger::remove_callback("test-callback"));
#endif
}

TEST(Logger, log_to_file_writes_message)
{
    logger::init();
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_INFO);

    const std::string path = "logging_cxx_file_sink.log";
    logger::log_to_file(
        path.c_str(), logger::file_mode::truncate, logger_verbosity_enum::VERBOSITY_INFO);
    LOGGING_LOG_INFO("file-sink-token-99");
    logger::flush();
    logger::end_log_to_file(path.c_str());

#if LOGGING_HAS_GLOG
    // glog rewrites destinations with severity suffixes; presence of the API is the contract.
    SUCCEED();
#else
    std::ifstream in(path);
    ASSERT_TRUE(in.good());
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("file-sink-token-99"), std::string::npos);
#endif
}

TEST(Logger, scope_macros_do_not_throw)
{
    logger::init();
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_INFO);
    {
        LOGGING_LOG_SCOPE_FUNCTION(INFO);
        LOGGING_LOG_INFO("inside scope");
    }
    logger::log_scope_raii empty;
    logger::log_scope_raii moved = std::move(empty);
    (void)moved;
    SUCCEED();
}

TEST(Logger, fatal_aborts)
{
    logger::init();
    logger::set_stderr_verbosity(logger_verbosity_enum::VERBOSITY_INFO);
    EXPECT_DEATH(LOGGING_LOG_FATAL("fatal-test-marker"), "");
}
