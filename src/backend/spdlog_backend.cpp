#include "include/logging/backend.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "include/logger/logger.h"
#include "include/logger/logger_verbosity_enum.h"

#if LOGGING_HAS_SPDLOG
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/spdlog.h>
#endif

namespace logging
{
namespace backend
{
namespace
{

#if LOGGING_HAS_SPDLOG

using logger_verbosity_enum = logging::logger_verbosity_enum;

std::once_flag                                                        g_init_flag;
std::mutex                                                            g_sinks_mutex;
std::shared_ptr<spdlog::sinks::dist_sink_mt>                          g_dist_sink;
std::shared_ptr<spdlog::logger>                                       g_logger;
std::shared_ptr<spdlog::sinks::sink>                                  g_stderr_sink;
std::unordered_map<std::string, std::shared_ptr<spdlog::sinks::sink>> g_file_sinks;
std::atomic<bool>                                                     g_console_mode{true};

thread_local char g_thread_name[128] = {};

spdlog::level::level_enum to_spdlog_min_level(int verbosity_int)
{
    auto verbosity = static_cast<logger_verbosity_enum>(verbosity_int);
    if (verbosity <= logger_verbosity_enum::VERBOSITY_OFF)
        return spdlog::level::off;
    if (verbosity <= logger_verbosity_enum::VERBOSITY_FATAL)
        return spdlog::level::critical;
    if (verbosity <= logger_verbosity_enum::VERBOSITY_ERROR)
        return spdlog::level::err;
    if (verbosity <= logger_verbosity_enum::VERBOSITY_WARNING)
        return spdlog::level::warn;
    if (verbosity <= logger_verbosity_enum::VERBOSITY_INFO)
        return spdlog::level::info;
    return spdlog::level::trace;
}

spdlog::level::level_enum to_spdlog_msg_level(int verbosity_int)
{
    auto verbosity = static_cast<logger_verbosity_enum>(verbosity_int);
    switch (verbosity)
    {
    case logger_verbosity_enum::VERBOSITY_FATAL:
        return spdlog::level::critical;
    case logger_verbosity_enum::VERBOSITY_ERROR:
        return spdlog::level::err;
    case logger_verbosity_enum::VERBOSITY_WARNING:
        return spdlog::level::warn;
    case logger_verbosity_enum::VERBOSITY_INFO:
        return spdlog::level::info;
    default:
        return (verbosity > logger_verbosity_enum::VERBOSITY_INFO) ? spdlog::level::trace
                                                                   : spdlog::level::critical;
    }
}

void ensure_logger()
{
    std::call_once(g_init_flag,
        []()
        {
            g_dist_sink      = std::make_shared<spdlog::sinks::dist_sink_mt>();
            auto stderr_sink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
            stderr_sink->set_pattern("%^[%l]%$ %s:%# %v");
            g_stderr_sink = stderr_sink;
            g_dist_sink->add_sink(g_stderr_sink);
            if (!g_console_mode.load(std::memory_order_relaxed))
            {
                g_stderr_sink->set_level(spdlog::level::off);
            }
            g_logger = std::make_shared<spdlog::logger>("logging", g_dist_sink);
            g_logger->set_level(spdlog::level::info);
            g_logger->flush_on(spdlog::level::err);
        });
}

void ensure_parent_directory(const char* path)
{
    if (path == nullptr)
        return;

    std::string dir(path);
    size_t      pos = dir.find_last_of("/\\");
    if (pos == std::string::npos)
        return;

    dir = dir.substr(0, pos);
    if (dir.empty())
        return;

    try
    {
        std::filesystem::create_directories(dir);
    }
    catch (...)
    {
    }
}

void abort_if_fatal(int verbosity_int)
{
    if (verbosity_int == static_cast<int>(logger_verbosity_enum::VERBOSITY_FATAL))
    {
        std::abort();
    }
}

#endif  // LOGGING_HAS_SPDLOG

}  // namespace

#if LOGGING_HAS_SPDLOG

class SpdlogBackend : public Backend
{
public:
    void log(int verbosity, const char* fname, unsigned int lineno, const char* message) override;
    void set_cutoff(int verbosity) override;
    int  get_cutoff() const override;
    void log_to_file(const char* path, bool truncate, int verbosity) override;
    void end_log_to_file(const char* path) override;
    void set_console_mode(bool enabled) override;
    bool get_console_mode() const override;
    void add_callback(const char* id,
        void*                     log_handler_ptr,
        void*                     user_data,
        int                       verbosity,
        void*                     on_close_ptr = nullptr,
        void*                     on_flush_ptr = nullptr) override;
    bool remove_callback(const char* id) override;
    void flush() override;
    void shutdown() override;
    void set_thread_name(std::string_view name) override;
};

void SpdlogBackend::log(int verbosity, const char* fname, unsigned int lineno, const char* message)
{
    ensure_logger();
    if (message == nullptr)
        message = "";
    g_logger->log(spdlog::source_loc{fname, static_cast<int>(lineno), ""},
        to_spdlog_msg_level(verbosity),
        "{}",
        message);
    abort_if_fatal(verbosity);
}

void SpdlogBackend::set_cutoff(int verbosity)
{
    ensure_logger();
    g_logger->set_level(to_spdlog_min_level(verbosity));
}

int SpdlogBackend::get_cutoff() const
{
    ensure_logger();
    return static_cast<int>(to_spdlog_msg_level(0));
}

void SpdlogBackend::log_to_file(const char* path, bool truncate, int verbosity)
{
    if (path == nullptr || *path == '\0')
    {
        return;
    }
    ensure_parent_directory(path);
    ensure_logger();
    const std::scoped_lock guard(g_sinks_mutex);

    auto it = g_file_sinks.find(path);
    if (it != g_file_sinks.end())
    {
        it->second->set_level(to_spdlog_min_level(verbosity));
        return;
    }

    try
    {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, truncate);
        file_sink->set_level(to_spdlog_min_level(verbosity));
        g_dist_sink->add_sink(file_sink);
        g_file_sinks[path] = file_sink;
    }
    catch (...)
    {
    }
}

void SpdlogBackend::end_log_to_file(const char* path)
{
    if (path == nullptr)
    {
        return;
    }
    const std::scoped_lock guard(g_sinks_mutex);
    auto                   it = g_file_sinks.find(path);
    if (it != g_file_sinks.end())
    {
        g_dist_sink->remove_sink(it->second);
        g_file_sinks.erase(it);
    }
}

void SpdlogBackend::set_console_mode(bool enabled)
{
    g_console_mode.store(enabled, std::memory_order_relaxed);
    ensure_logger();
    const std::scoped_lock guard(g_sinks_mutex);
    if (g_stderr_sink != nullptr)
    {
        g_stderr_sink->set_level(enabled ? spdlog::level::trace : spdlog::level::off);
    }
}

bool SpdlogBackend::get_console_mode() const
{
    return g_console_mode.load(std::memory_order_relaxed);
}

void SpdlogBackend::add_callback(const char* id,
    void*                                    log_handler_ptr,
    void*                                    user_data,
    int                                      verbosity,
    void*                                    on_close_ptr,
    void*                                    on_flush_ptr)
{
    if (id == nullptr)
    {
        return;
    }
    ensure_logger();
    // Placeholder: spdlog callback integration
    (void)log_handler_ptr;
    (void)user_data;
    (void)verbosity;
    (void)on_close_ptr;
    (void)on_flush_ptr;
}

bool SpdlogBackend::remove_callback(const char* id)
{
    if (id == nullptr)
    {
        return false;
    }
    // Placeholder
    (void)id;
    return false;
}

void SpdlogBackend::flush()
{
    ensure_logger();
    g_logger->flush();
}

void SpdlogBackend::shutdown()
{
    ensure_logger();
    spdlog::shutdown();
}

void SpdlogBackend::set_thread_name(std::string_view name)
{
    if (name.size() >= sizeof(g_thread_name))
    {
        std::strncpy(g_thread_name, name.data(), sizeof(g_thread_name) - 1);
        g_thread_name[sizeof(g_thread_name) - 1] = '\0';
    }
    else
    {
        std::strcpy(g_thread_name, name.data());
    }
}

std::unique_ptr<Backend> create_spdlog_backend()
{
    return std::make_unique<SpdlogBackend>();
}

#else

std::unique_ptr<Backend> create_spdlog_backend()
{
    return nullptr;
}

#endif  // LOGGING_HAS_SPDLOG

}  // namespace backend
}  // namespace logging
