/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 */

// Micro-benchmarks for the compile-time logging backend (one of spdlog, loguru,
// glog, native). Reconfigure with --logging=BACKEND and run this binary in
// Release for a fair comparison. ctest uses --benchmark_min_time=0.01s.
//
// LOGGING_LOG checks get_current_verbosity_cutoff() before formatting. Enabled
// benches therefore set VERBOSITY_INFO. stderr is redirected to /dev/null so
// the File/Callback numbers measure the backend, not the terminal.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "include/logger/logger.h"

namespace
{
const char* backend_name()
{
#if LOGGING_HAS_SPDLOG
    return "spdlog";
#elif LOGGING_HAS_LOGURU
    return "loguru";
#elif LOGGING_HAS_GLOG
    return "glog";
#elif LOGGING_HAS_NATIVE
    return "native";
#else
    return "unknown";
#endif
}

void discard_callback(void* /*user_data*/, const logging::logger::Message& /*message*/) {}

std::string file_path_for_thread(std::int64_t thread_index)
{
    const auto dir = std::filesystem::temp_directory_path() / "logging_bench";
    std::filesystem::create_directories(dir);
    return (dir /
            ("bench_" + std::string(backend_name()) + "_" + std::to_string(thread_index) + ".log"))
        .string();
}

std::once_flag g_init_once;

void ensure_logger_init()
{
    std::call_once(g_init_once,
        []()
        {
            logging::logger::set_enable_unsafe_signal_handler(false);
            logging::logger::init();
        });
}

class stderr_silencer
{
public:
    stderr_silencer()
    {
#if defined(_WIN32)
        saved_    = _dup(_fileno(stderr));
        FILE* nul = nullptr;
        (void)freopen_s(&nul, "NUL", "w", stderr);
#else
        saved_            = dup(fileno(stderr));
        const int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0)
        {
            dup2(null_fd, fileno(stderr));
            close(null_fd);
        }
#endif
    }

    ~stderr_silencer()
    {
        if (saved_ < 0)
        {
            return;
        }
#if defined(_WIN32)
        _dup2(saved_, _fileno(stderr));
        _close(saved_);
#else
        dup2(saved_, fileno(stderr));
        close(saved_);
#endif
    }

    stderr_silencer(const stderr_silencer&)            = delete;
    stderr_silencer& operator=(const stderr_silencer&) = delete;

private:
    int saved_{-1};
};

}  // namespace

namespace
{
void disabled_info(benchmark::State& state)
{
    ensure_logger_init();
    logging::logger::set_stderr_verbosity(logging::logger_verbosity_enum::VERBOSITY_OFF);
    std::int64_t i = 0;
    for (auto _ : state)
    {
        LOGGING_LOG_INFO("disabled {}", i);
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel(backend_name());
}

void file_literal(benchmark::State& state)
{
    ensure_logger_init();
    logging::logger::set_stderr_verbosity(logging::logger_verbosity_enum::VERBOSITY_INFO);
    const stderr_silencer silence;
    const std::string     path = file_path_for_thread(state.thread_index());
    logging::logger::log_to_file(path.c_str(),
        logging::logger::file_mode::truncate,
        logging::logger_verbosity_enum::VERBOSITY_INFO);
    for (auto _ : state)
    {
        LOGGING_LOG_INFO("hello world");
    }
    logging::logger::flush();
    logging::logger::end_log_to_file(path.c_str());
    state.SetItemsProcessed(state.iterations());
    state.SetLabel(backend_name());
}

void file_formatted(benchmark::State& state)
{
    ensure_logger_init();
    logging::logger::set_stderr_verbosity(logging::logger_verbosity_enum::VERBOSITY_INFO);
    const stderr_silencer silence;
    const std::string     path = file_path_for_thread(state.thread_index());
    logging::logger::log_to_file(path.c_str(),
        logging::logger::file_mode::truncate,
        logging::logger_verbosity_enum::VERBOSITY_INFO);
    std::int64_t i = 0;
    for (auto _ : state)
    {
        LOGGING_LOG_INFO("iter {} value {}", i, 3.14159);
        ++i;
    }
    logging::logger::flush();
    logging::logger::end_log_to_file(path.c_str());
    state.SetItemsProcessed(state.iterations());
    state.SetLabel(backend_name());
}

void callback_discard(benchmark::State& state)
{
    ensure_logger_init();
#if LOGGING_HAS_GLOG
    state.SkipWithError("glog does not support callbacks");
    return;
#else
    logging::logger::set_stderr_verbosity(logging::logger_verbosity_enum::VERBOSITY_INFO);
    const stderr_silencer silence;
    logging::logger::add_callback(
        "bench-discard", discard_callback, nullptr, logging::logger_verbosity_enum::VERBOSITY_INFO);
    std::int64_t i = 0;
    for (auto _ : state)
    {
        LOGGING_LOG_INFO("callback {}", i);
        ++i;
    }
    logging::logger::remove_callback("bench-discard");
    state.SetItemsProcessed(state.iterations());
    state.SetLabel(backend_name());
#endif
}
}  // namespace

int main(int argc, char** argv)
{
    try
    {
        benchmark::RegisterBenchmark("LoggerBench/DisabledInfo", disabled_info)
            ->Threads(1)
            ->Threads(4);
        benchmark::RegisterBenchmark("LoggerBench/FileLiteral", file_literal)->Threads(1);
        benchmark::RegisterBenchmark("LoggerBench/FileFormatted", file_formatted)->Threads(1);
        benchmark::RegisterBenchmark("LoggerBench/CallbackDiscard", callback_discard)->Threads(1);
        benchmark::Initialize(&argc, argv);
        if (benchmark::ReportUnrecognizedArguments(argc, argv))
        {
            return 1;
        }
        benchmark::RunSpecifiedBenchmarks();
        benchmark::Shutdown();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "benchmark startup failed: %s\n", error.what());
        return 1;
    }
    return 0;
}
