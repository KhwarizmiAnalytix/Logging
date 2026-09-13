#include "logger/logger.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "common/logging_macros.h"
#include "logger_verbosity_enum.h"

#if LOGGING_HAS_LOGURU
#include <loguru.hpp>
#elif LOGGING_HAS_GLOG
#include <glog/logging.h>
#elif LOGGING_HAS_NATIVE
#include <fmt/color.h>

#include <chrono>
#elif LOGGING_HAS_SPDLOG
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/spdlog.h>

#include <chrono>
#endif

namespace
{
std::atomic<bool>              g_console_mode{true};
logging::logger_verbosity_enum g_requested_stderr_verbosity =
    logging::logger_verbosity_enum::VERBOSITY_INFO;
#if LOGGING_HAS_GLOG
bool g_glog_has_file = false;
#endif

#if defined(_WIN32)
void ensure_windows_console()
{
    if (GetConsoleWindow() != nullptr)
    {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0)
    {
        if (AllocConsole() == 0)
        {
            return;
        }
        SetConsoleTitleW(L"XSigma logging");
    }
    FILE* stream = nullptr;
    (void)freopen_s(&stream, "CONOUT$", "w", stdout);
    (void)freopen_s(&stream, "CONOUT$", "w", stderr);
    (void)freopen_s(&stream, "CONIN$", "r", stdin);
}
#endif
}  // namespace

namespace logging
{
namespace
{
void copy_thread_name(char* dest, std::size_t dest_size, const std::string& name)
{
    if ((dest == nullptr) || dest_size == 0)
    {
        return;
    }
    std::strncpy(dest, name.c_str(), dest_size - 1);
    dest[dest_size - 1] = '\0';
}

std::string vformat_printf(const char* format, va_list args)
{
    if (format == nullptr)
    {
        return {};
    }
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (needed <= 0)
    {
        return {};
    }
    std::string result(static_cast<std::size_t>(needed) + 1U, '\0');
    std::vsnprintf(result.data(), result.size(), format, args);
    result.resize(static_cast<std::size_t>(needed));
    return result;
}

void abort_if_fatal(logger_verbosity_enum verbosity)
{
    if (verbosity == logger_verbosity_enum::VERBOSITY_FATAL)
    {
        std::abort();
    }
}

void ensure_parent_directory(const char* path)
{
    if ((path == nullptr) || *path == '\0')
    {
        return;
    }
    const std::filesystem::path file_path(path);
    if (!file_path.has_parent_path())
    {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(file_path.parent_path(), error);
}

const char* basename_from_path(const char* fname)
{
    if (fname == nullptr)
    {
        return "";
    }
    const char* filename = fname;
    for (const char* p = fname; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            filename = p + 1;
        }
    }
    return filename;
}
}  // namespace
}  // namespace logging

//=============================================================================
#if LOGGING_HAS_NATIVE

namespace logging
{
namespace internal
{
static const char* verbosity_to_string(logger_verbosity_enum severity)
{
    switch (severity)
    {
    case logger_verbosity_enum::VERBOSITY_FATAL:
        return "FATAL";
    case logger_verbosity_enum::VERBOSITY_ERROR:
        return "ERROR";
    case logger_verbosity_enum::VERBOSITY_WARNING:
        return "WARNING";
    case logger_verbosity_enum::VERBOSITY_INFO:
        return "INFO";
    case logger_verbosity_enum::VERBOSITY_TRACE:
        return "TRACE";
    default:
        return "VLOG";
    }
}

static fmt::color get_severity_color(logger_verbosity_enum severity)
{
    switch (severity)
    {
    case logger_verbosity_enum::VERBOSITY_FATAL:
    case logger_verbosity_enum::VERBOSITY_ERROR:
        return fmt::color::red;
    case logger_verbosity_enum::VERBOSITY_WARNING:
        return fmt::color::yellow;
    case logger_verbosity_enum::VERBOSITY_INFO:
        return fmt::color::green;
    default:
        return fmt::color::white;
    }
}

static std::atomic<int> g_cutoff{static_cast<int>(logger_verbosity_enum::VERBOSITY_INFO)};
static std::mutex       g_io_mutex;

struct file_sink
{
    std::string           path;
    std::ofstream         stream;
    logger_verbosity_enum verbosity{logger_verbosity_enum::VERBOSITY_INFO};
};

static std::vector<file_sink> g_files;

struct callback_entry
{
    logger::log_handler_callback_t   callback{nullptr};
    logger::close_handler_callback_t on_close{nullptr};
    logger::flush_handler_callback_t on_flush{nullptr};
    void*                            user_data{nullptr};
    logger_verbosity_enum            verbosity{logger_verbosity_enum::VERBOSITY_INFO};
};

static std::unordered_map<std::string, callback_entry> g_callbacks;
static thread_local char                               ThreadName[128] = {};

static std::string format_line(
    const char* fname, unsigned lineno, logger_verbosity_enum severity, const std::string& message)
{
    const char* thread = ThreadName;
    if (thread[0] != '\0')
    {
        return fmt::format(
            "[{}] [{}] {}:{} {}",
            verbosity_to_string(severity),
            thread,
            basename_from_path(fname),
            lineno,
            message);
    }
    return fmt::format(
        "[{}] {}:{} {}", verbosity_to_string(severity), basename_from_path(fname), lineno, message);
}

void native_log_output(
    const char* fname, unsigned lineno, logger_verbosity_enum severity, const std::string& message)
{
    if (message.empty() && severity != logger_verbosity_enum::VERBOSITY_FATAL)
    {
        return;
    }

    const int cutoff = g_cutoff.load(std::memory_order_relaxed);
    if (static_cast<int>(severity) > cutoff && severity != logger_verbosity_enum::VERBOSITY_FATAL)
    {
        return;
    }

    const std::string line = format_line(fname, lineno, severity, message);

    logger::Message payload;
    payload.verbosity = severity;
    payload.filename  = basename_from_path(fname);
    payload.line      = lineno;
    payload.preamble  = line;
    payload.message   = message;

    std::vector<callback_entry> callbacks_copy;
    {
        const std::scoped_lock guard(g_io_mutex);
        if (g_console_mode.load(std::memory_order_relaxed) ||
            severity == logger_verbosity_enum::VERBOSITY_FATAL)
        {
            fmt::print(stderr, fg(get_severity_color(severity)), "{}\n", line);
        }

        for (auto& sink : g_files)
        {
            if (severity <= sink.verbosity && sink.stream.is_open())
            {
                sink.stream << line << '\n';
            }
        }

        callbacks_copy.reserve(g_callbacks.size());
        for (const auto& [id, entry] : g_callbacks)
        {
            (void)id;
            if (entry.callback != nullptr && severity <= entry.verbosity)
            {
                callbacks_copy.push_back(entry);
            }
        }
    }

    for (const auto& entry : callbacks_copy)
    {
        entry.callback(entry.user_data, payload);
    }

    abort_if_fatal(severity);
}

int native_cutoff()
{
    return g_cutoff.load(std::memory_order_relaxed);
}

void native_set_cutoff(logger_verbosity_enum level)
{
    g_cutoff.store(static_cast<int>(level), std::memory_order_relaxed);
}

void native_log_to_file(const char* path, logger::file_mode mode, logger_verbosity_enum verbosity)
{
    if ((path == nullptr) || *path == '\0')
    {
        return;
    }
    ensure_parent_directory(path);
    const std::scoped_lock guard(g_io_mutex);
    for (auto& sink : g_files)
    {
        if (sink.path == path)
        {
            sink.verbosity = verbosity;
            return;
        }
    }
    file_sink sink;
    sink.path            = path;
    sink.verbosity       = verbosity;
    const auto open_mode = (mode == logger::file_mode::append) ? (std::ios::out | std::ios::app)
                                                               : (std::ios::out | std::ios::trunc);
    sink.stream.open(path, open_mode);
    g_files.push_back(std::move(sink));
}

void native_end_log_to_file(const char* path)
{
    if (path == nullptr)
    {
        return;
    }
    const std::scoped_lock guard(g_io_mutex);
    for (auto it = g_files.begin(); it != g_files.end(); ++it)
    {
        if (it->path == path)
        {
            if (it->stream.is_open())
            {
                it->stream.flush();
                it->stream.close();
            }
            g_files.erase(it);
            return;
        }
    }
}

void native_flush()
{
    const std::scoped_lock guard(g_io_mutex);
    std::fflush(stderr);
    for (auto& sink : g_files)
    {
        if (sink.stream.is_open())
        {
            sink.stream.flush();
        }
    }
    for (auto& [id, entry] : g_callbacks)
    {
        (void)id;
        if (entry.on_flush != nullptr)
        {
            entry.on_flush(entry.user_data);
        }
    }
}

void native_add_callback(
    const char*                      id,
    logger::log_handler_callback_t   callback,
    void*                            user_data,
    logger_verbosity_enum            verbosity,
    logger::close_handler_callback_t on_close,
    logger::flush_handler_callback_t on_flush)
{
    if (id == nullptr)
    {
        return;
    }
    const std::scoped_lock guard(g_io_mutex);
    g_callbacks[id] = callback_entry{callback, on_close, on_flush, user_data, verbosity};
}

bool native_remove_callback(const char* id)
{
    if (id == nullptr)
    {
        return false;
    }
    const std::scoped_lock guard(g_io_mutex);
    auto                   it = g_callbacks.find(id);
    if (it == g_callbacks.end())
    {
        return false;
    }
    if (it->second.on_close != nullptr)
    {
        it->second.on_close(it->second.user_data);
    }
    g_callbacks.erase(it);
    return true;
}

}  // namespace internal
}  // namespace logging

#endif  // LOGGING_HAS_NATIVE

//=============================================================================
#if LOGGING_HAS_SPDLOG

namespace logging
{
namespace spdlog_backend
{
static std::once_flag                                                        g_init_flag;
static std::mutex                                                            g_sinks_mutex;
static std::shared_ptr<spdlog::sinks::dist_sink_mt>                          g_dist_sink;
static std::shared_ptr<spdlog::logger>                                       g_logger;
static std::shared_ptr<spdlog::sinks::sink>                                  g_stderr_sink;
static std::unordered_map<std::string, std::shared_ptr<spdlog::sinks::sink>> g_file_sinks;

struct CallbackEntry
{
    std::shared_ptr<spdlog::sinks::sink> sink;
    logger::close_handler_callback_t     on_close{nullptr};
    logger::flush_handler_callback_t     on_flush{nullptr};
    void*                                user_data{nullptr};
};
static std::unordered_map<std::string, CallbackEntry> g_callback_sinks;
static thread_local char                              ThreadName[128] = {};

static spdlog::level::level_enum to_spdlog_min_level(logger_verbosity_enum v)
{
    if (v <= logger_verbosity_enum::VERBOSITY_OFF)
        return spdlog::level::off;
    if (v <= logger_verbosity_enum::VERBOSITY_FATAL)
        return spdlog::level::critical;
    if (v <= logger_verbosity_enum::VERBOSITY_ERROR)
        return spdlog::level::err;
    if (v <= logger_verbosity_enum::VERBOSITY_WARNING)
        return spdlog::level::warn;
    if (v <= logger_verbosity_enum::VERBOSITY_INFO)
        return spdlog::level::info;
    return spdlog::level::trace;
}

static spdlog::level::level_enum to_spdlog_msg_level(logger_verbosity_enum v)
{
    switch (v)
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
        return (v > logger_verbosity_enum::VERBOSITY_INFO) ? spdlog::level::trace
                                                           : spdlog::level::critical;
    }
}

static logger_verbosity_enum from_spdlog_level(spdlog::level::level_enum l)
{
    switch (l)
    {
    case spdlog::level::off:
        return logger_verbosity_enum::VERBOSITY_OFF;
    case spdlog::level::critical:
        return logger_verbosity_enum::VERBOSITY_FATAL;
    case spdlog::level::err:
        return logger_verbosity_enum::VERBOSITY_ERROR;
    case spdlog::level::warn:
        return logger_verbosity_enum::VERBOSITY_WARNING;
    case spdlog::level::info:
        return logger_verbosity_enum::VERBOSITY_INFO;
    default:
        return logger_verbosity_enum::VERBOSITY_TRACE;
    }
}

static void ensure_logger()
{
    std::call_once(
        g_init_flag,
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

static void set_console_sink_enabled(bool enabled)
{
    ensure_logger();
    const std::scoped_lock guard(g_sinks_mutex);
    if (g_stderr_sink != nullptr)
    {
        g_stderr_sink->set_level(enabled ? spdlog::level::trace : spdlog::level::off);
    }
}

}  // namespace spdlog_backend
}  // namespace logging

#endif  // LOGGING_HAS_SPDLOG

//=============================================================================
namespace logging
{
class logger::log_scope_raii::ls_internals
{
public:
#if LOGGING_HAS_LOGURU
    std::unique_ptr<loguru::LogScopeRAII> data;
#elif LOGGING_HAS_GLOG || LOGGING_HAS_NATIVE || LOGGING_HAS_SPDLOG
    std::string           scope_message;
    std::string           fname;
    int                   lineno{0};
    logger_verbosity_enum verbosity{logger_verbosity_enum::VERBOSITY_INFO};
#if LOGGING_HAS_SPDLOG || LOGGING_HAS_NATIVE
    std::chrono::steady_clock::time_point entry_time;
#endif
#endif
};

logger::log_scope_raii::log_scope_raii() = default;

logger::log_scope_raii::log_scope_raii(log_scope_raii&&) noexcept                    = default;
logger::log_scope_raii& logger::log_scope_raii::operator=(log_scope_raii&&) noexcept = default;

// NOLINTNEXTLINE(modernize-avoid-variadic-functions)
logger::log_scope_raii::log_scope_raii(
    logger_verbosity_enum verbosity,
    const char*           fname,
    unsigned int          lineno,
    const char*           format,
    ...)
{
#if LOGGING_HAS_LOGURU || LOGGING_HAS_GLOG || LOGGING_HAS_NATIVE || LOGGING_HAS_SPDLOG
    va_list vlist;
    va_start(vlist, format);
    const std::string formatted = vformat_printf(format, vlist);
    va_end(vlist);

#if LOGGING_HAS_LOGURU
    internals_       = std::make_unique<ls_internals>();
    internals_->data = std::make_unique<loguru::LogScopeRAII>(
        static_cast<loguru::Verbosity>(verbosity), fname, lineno, "%s", formatted.c_str());
#elif LOGGING_HAS_GLOG
    internals_                = std::make_unique<ls_internals>();
    internals_->scope_message = formatted;
    internals_->fname         = fname ? fname : "";
    internals_->lineno        = static_cast<int>(lineno);
    internals_->verbosity     = verbosity;
    logger::log(verbosity, fname, lineno, ("[scope enter] " + formatted).c_str());
#elif LOGGING_HAS_NATIVE
    internals_                = std::make_unique<ls_internals>();
    internals_->scope_message = formatted;
    internals_->fname         = fname ? fname : "";
    internals_->lineno        = static_cast<int>(lineno);
    internals_->verbosity     = verbosity;
    internals_->entry_time    = std::chrono::steady_clock::now();
    logger::log(verbosity, fname, lineno, ("[scope enter] " + formatted).c_str());
#elif LOGGING_HAS_SPDLOG
    internals_                = std::make_unique<ls_internals>();
    internals_->scope_message = formatted;
    internals_->fname         = fname ? fname : "";
    internals_->lineno        = static_cast<int>(lineno);
    internals_->verbosity     = verbosity;
    internals_->entry_time    = std::chrono::steady_clock::now();
    spdlog_backend::ensure_logger();
    spdlog_backend::g_logger->log(
        spdlog::source_loc{fname, static_cast<int>(lineno), ""},
        spdlog_backend::to_spdlog_msg_level(verbosity),
        "[scope enter] {}",
        formatted);
#else
    (void)verbosity;
    (void)fname;
    (void)lineno;
    (void)format;
#endif
#else
    (void)verbosity;
    (void)fname;
    (void)lineno;
    (void)format;
#endif
}

logger::log_scope_raii::~log_scope_raii()
{
    if (!internals_)
    {
        return;
    }
#if LOGGING_HAS_SPDLOG
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - internals_->entry_time)
                                .count();
    spdlog_backend::g_logger->log(
        spdlog::source_loc{internals_->fname.c_str(), internals_->lineno, ""},
        spdlog_backend::to_spdlog_msg_level(internals_->verbosity),
        "[scope exit]  {} ({} us)",
        internals_->scope_message,
        elapsed_us);
#elif LOGGING_HAS_NATIVE
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - internals_->entry_time)
                                .count();
    const std::string msg =
        fmt::format("[scope exit]  {} ({} us)", internals_->scope_message, elapsed_us);
    logger::log(
        internals_->verbosity,
        internals_->fname.c_str(),
        static_cast<unsigned>(internals_->lineno),
        msg.c_str());
#elif LOGGING_HAS_GLOG
    logger::log(
        internals_->verbosity,
        internals_->fname.c_str(),
        static_cast<unsigned>(internals_->lineno),
        ("[scope exit] " + internals_->scope_message).c_str());
#endif
}

//=============================================================================
namespace detail
{
static thread_local char ThreadName[128] = {};

struct named_scope
{
    std::string           id;
    logger_verbosity_enum verbosity{logger_verbosity_enum::VERBOSITY_INFO};
    std::string           fname;
    unsigned              lineno{0};
};

static std::mutex                                                     g_scope_mutex;
static std::unordered_map<std::thread::id, std::vector<named_scope>>& scope_vectors()
{
    static std::unordered_map<std::thread::id, std::vector<named_scope>> vectors;
    return vectors;
}

static std::vector<named_scope>& get_vector()
{
    const std::scoped_lock guard(g_scope_mutex);
    return scope_vectors()[std::this_thread::get_id()];
}

static void push_named_scope(named_scope scope)
{
    get_vector().push_back(std::move(scope));
}

static void pop_named_scope(const char* id)
{
    auto& vector = get_vector();
    if (vector.empty())
    {
        logger::log(
            logger_verbosity_enum::VERBOSITY_ERROR,
            __FILE__,
            __LINE__,
            fmt::format("Mismatched scope! stack empty, got ({})", id ? id : "").c_str());
        return;
    }
    if (id != nullptr && vector.back().id == id)
    {
        const named_scope finished = vector.back();
        vector.pop_back();
        if (vector.empty())
        {
            const std::scoped_lock guard(g_scope_mutex);
            scope_vectors().erase(std::this_thread::get_id());
        }
        logger::log(
            finished.verbosity,
            finished.fname.c_str(),
            finished.lineno,
            fmt::format("[scope exit] {}", finished.id).c_str());
        return;
    }
    logger::log(
        logger_verbosity_enum::VERBOSITY_ERROR,
        __FILE__,
        __LINE__,
        fmt::format("Mismatched scope! expected ({}), got ({})", vector.back().id, id ? id : "")
            .c_str());
}

#if LOGGING_HAS_LOGURU
using scope_pair = std::pair<std::string, std::shared_ptr<loguru::LogScopeRAII>>;
static std::mutex g_loguru_mutex;

static std::unordered_map<std::thread::id, std::vector<scope_pair>>& loguru_scope_vectors()
{
    static std::unordered_map<std::thread::id, std::vector<scope_pair>> vectors;
    return vectors;
}

static std::vector<scope_pair>& loguru_get_vector()
{
    const std::scoped_lock guard(g_loguru_mutex);
    return loguru_scope_vectors()[std::this_thread::get_id()];
}

static void loguru_push_scope(const char* id, std::shared_ptr<loguru::LogScopeRAII> ptr)
{
    loguru_get_vector().emplace_back(std::string(id ? id : ""), std::move(ptr));
}

static void loguru_pop_scope(const char* id)
{
    auto& vector = loguru_get_vector();
    if (vector.empty())
    {
        LOG_F(ERROR, "Mismatched scope! stack empty, got (%s)", id ? id : "");
        return;
    }
    if (id != nullptr && vector.back().first == id)
    {
        vector.pop_back();
        if (vector.empty())
        {
            const std::scoped_lock guard(g_loguru_mutex);
            loguru_scope_vectors().erase(std::this_thread::get_id());
        }
        return;
    }
    LOG_F(
        ERROR,
        "Mismatched scope! expected (%s), got (%s)",
        vector.back().first.c_str(),
        id ? id : "");
}
#endif
}  // namespace detail

//=============================================================================
bool                  logger::enable_unsafe_signal_handler = true;
bool                  logger::enable_sigabrt_handler       = false;
bool                  logger::enable_sigbus_handler        = false;
bool                  logger::enable_sigfpe_handler        = false;
bool                  logger::enable_sigill_handler        = false;
bool                  logger::enable_sigint_handler        = false;
bool                  logger::enable_sigsegv_handler       = false;
bool                  logger::enable_sigterm_handler       = false;
logger_verbosity_enum logger::internal_verbosity_level_    = logger_verbosity_enum::VERBOSITY_INFO;

logger::logger()  = default;
logger::~logger() = default;

void logger::set_enable_unsafe_signal_handler(bool enabled)
{
    enable_unsafe_signal_handler = enabled;
}

bool logger::get_enable_unsafe_signal_handler()
{
    return enable_unsafe_signal_handler;
}

namespace
{
#if LOGGING_HAS_GLOG
void apply_glog_verbosity_flags(logging::logger_verbosity_enum level)
{
    if (level <= logging::logger_verbosity_enum::VERBOSITY_OFF)
    {
        FLAGS_minloglevel     = google::GLOG_FATAL + 1;
        FLAGS_stderrthreshold = google::GLOG_FATAL + 1;
        FLAGS_v               = 0;
    }
    else if (level <= logging::logger_verbosity_enum::VERBOSITY_FATAL)
    {
        FLAGS_minloglevel     = google::GLOG_FATAL;
        FLAGS_stderrthreshold = google::GLOG_FATAL;
        FLAGS_v               = 0;
    }
    else if (level <= logging::logger_verbosity_enum::VERBOSITY_ERROR)
    {
        FLAGS_minloglevel     = google::GLOG_ERROR;
        FLAGS_stderrthreshold = google::GLOG_ERROR;
        FLAGS_v               = 0;
    }
    else if (level <= logging::logger_verbosity_enum::VERBOSITY_WARNING)
    {
        FLAGS_minloglevel     = google::GLOG_WARNING;
        FLAGS_stderrthreshold = google::GLOG_WARNING;
        FLAGS_v               = 0;
    }
    else if (level <= logging::logger_verbosity_enum::VERBOSITY_INFO)
    {
        FLAGS_minloglevel     = google::GLOG_INFO;
        FLAGS_stderrthreshold = google::GLOG_INFO;
        FLAGS_v               = 0;
    }
    else
    {
        FLAGS_minloglevel     = google::GLOG_INFO;
        FLAGS_stderrthreshold = google::GLOG_INFO;
        FLAGS_v               = static_cast<int>(level);
    }
}
#endif

void apply_console_sinks()
{
    const bool enabled = g_console_mode.load(std::memory_order_relaxed);
#if defined(_WIN32)
    if (enabled)
    {
        ensure_windows_console();
    }
#endif
#if LOGGING_HAS_LOGURU
    loguru::g_stderr_verbosity = enabled
                                     ? static_cast<loguru::Verbosity>(g_requested_stderr_verbosity)
                                     : loguru::Verbosity_OFF;
#elif LOGGING_HAS_GLOG
    if (g_glog_has_file)
    {
        FLAGS_logtostderr     = false;
        FLAGS_alsologtostderr = enabled;
    }
    else
    {
        FLAGS_logtostderr     = enabled;
        FLAGS_alsologtostderr = false;
    }
    // glog still mirrors severity >= stderrthreshold to stderr when logtostderr is
    // false; silence that path entirely when console mode is off.
    if (!enabled)
    {
        FLAGS_stderrthreshold = google::GLOG_FATAL + 1;
    }
    else
    {
        apply_glog_verbosity_flags(g_requested_stderr_verbosity);
    }
#elif LOGGING_HAS_NATIVE
    (void)enabled;
#elif LOGGING_HAS_SPDLOG
    spdlog_backend::set_console_sink_enabled(enabled);
#else
    (void)enabled;
#endif
}
}  // namespace

void logger::set_console_mode(bool enabled)
{
    g_console_mode.store(enabled, std::memory_order_relaxed);
    apply_console_sinks();
}

bool logger::get_console_mode()
{
    return g_console_mode.load(std::memory_order_relaxed);
}

void logger::init(int& argc, char* argv[], const char* verbosity_flag)
{
#if LOGGING_HAS_LOGURU
    if (argc == 0)
    {
        logger::init();
        return;
    }

    loguru::g_preamble_date = false;
    loguru::g_preamble_time = false;
    loguru::g_internal_verbosity =
        static_cast<loguru::Verbosity>(logger::internal_verbosity_level_);

    const auto current_stderr_verbosity = loguru::g_stderr_verbosity;
    if (loguru::g_internal_verbosity > loguru::g_stderr_verbosity)
    {
        loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
    }
    loguru::Options options;
    options.verbosity_flag                       = verbosity_flag;
    options.signal_options.unsafe_signal_handler = logger::enable_unsafe_signal_handler;
    options.signal_options.sigabrt               = logger::enable_sigabrt_handler;
    options.signal_options.sigbus                = logger::enable_sigbus_handler;
    options.signal_options.sigfpe                = logger::enable_sigfpe_handler;
    options.signal_options.sigill                = logger::enable_sigill_handler;
    options.signal_options.sigint                = logger::enable_sigint_handler;
    options.signal_options.sigsegv               = logger::enable_sigsegv_handler;
    options.signal_options.sigterm               = logger::enable_sigterm_handler;
    if (std::strlen(detail::ThreadName) > 0)
    {
        options.main_thread_name = detail::ThreadName;
    }
    loguru::init(argc, argv, options);
    loguru::g_stderr_verbosity = current_stderr_verbosity;
#elif LOGGING_HAS_GLOG
    if (!google::IsGoogleLoggingInitialized())
    {
        google::InitGoogleLogging(argc > 0 && argv != nullptr ? argv[0] : "logging");
    }
    FLAGS_colorlogtostderr = true;

    if (verbosity_flag != nullptr)
    {
        for (int i = 1; i < argc - 1; ++i)
        {
            if (argv != nullptr && std::string(argv[i]) == verbosity_flag)
            {
                const auto parsed = logger::convert_to_verbosity(argv[i + 1]);
                if (parsed != logger_verbosity_enum::VERBOSITY_INVALID)
                {
                    logger::set_stderr_verbosity(parsed);
                }
                break;
            }
        }
    }
    if (logger::enable_unsafe_signal_handler)
    {
        google::InstallFailureSignalHandler();
    }
#elif LOGGING_HAS_NATIVE
    if (verbosity_flag != nullptr && argv != nullptr)
    {
        for (int i = 1; i < argc - 1; ++i)
        {
            if (std::string(argv[i]) == verbosity_flag)
            {
                const auto parsed = logger::convert_to_verbosity(argv[i + 1]);
                if (parsed != logger_verbosity_enum::VERBOSITY_INVALID)
                {
                    internal::native_set_cutoff(parsed);
                }
                break;
            }
        }
    }
#elif LOGGING_HAS_SPDLOG
    spdlog_backend::ensure_logger();
    if (verbosity_flag != nullptr && argv != nullptr)
    {
        for (int i = 1; i < argc - 1; ++i)
        {
            if (std::string(argv[i]) == verbosity_flag)
            {
                const auto v = logger::convert_to_verbosity(argv[i + 1]);
                if (v != logger_verbosity_enum::VERBOSITY_INVALID)
                {
                    spdlog_backend::g_logger->set_level(spdlog_backend::to_spdlog_min_level(v));
                }
                break;
            }
        }
    }
    if (std::strlen(spdlog_backend::ThreadName) > 0)
    {
        spdlog_backend::g_logger->set_pattern(
            fmt::format("[%^%l%$] [{}] %s:%# %v", spdlog_backend::ThreadName));
    }
#else
    (void)argc;
    (void)argv;
    (void)verbosity_flag;
#endif
    apply_console_sinks();
}

void logger::init()
{
    int                  argc  = 1;
    std::array<char, 1>  dummy = {'\0'};
    std::array<char*, 2> argv  = {dummy.data(), nullptr};
    logger::init(argc, argv.data());
}

void logger::set_stderr_verbosity(logger_verbosity_enum level)
{
    g_requested_stderr_verbosity = level;
#if LOGGING_HAS_LOGURU
    apply_console_sinks();
#elif LOGGING_HAS_GLOG
    apply_glog_verbosity_flags(level);
    apply_console_sinks();
#elif LOGGING_HAS_NATIVE
    internal::native_set_cutoff(level);
#elif LOGGING_HAS_SPDLOG
    spdlog_backend::ensure_logger();
    spdlog_backend::g_logger->set_level(spdlog_backend::to_spdlog_min_level(level));
#else
    (void)level;
#endif
}

void logger::set_internal_verbosity_level(logger_verbosity_enum level)
{
    logger::internal_verbosity_level_ = level;
#if LOGGING_HAS_LOGURU
    loguru::g_internal_verbosity = static_cast<loguru::Verbosity>(level);
#elif LOGGING_HAS_GLOG
    FLAGS_v = static_cast<int>(level);
#elif LOGGING_HAS_NATIVE
    internal::native_set_cutoff(level);
#elif LOGGING_HAS_SPDLOG
    spdlog_backend::ensure_logger();
    spdlog_backend::g_logger->set_level(spdlog_backend::to_spdlog_min_level(level));
#else
    (void)level;
#endif
}

void logger::log_to_file(const char* path, logger::file_mode mode, logger_verbosity_enum verbosity)
{
    if ((path == nullptr) || *path == '\0')
    {
        return;
    }
    ensure_parent_directory(path);
#if LOGGING_HAS_LOGURU
    const loguru::FileMode loguru_mode =
        (mode == logger::file_mode::append) ? loguru::Append : loguru::Truncate;
    loguru::add_file(path, loguru_mode, static_cast<loguru::Verbosity>(verbosity));
#elif LOGGING_HAS_GLOG
    g_glog_has_file = true;
    apply_console_sinks();
    google::SetLogDestination(google::GLOG_INFO, path);
    google::SetLogDestination(google::GLOG_WARNING, path);
    google::SetLogDestination(google::GLOG_ERROR, path);
    google::SetLogDestination(google::GLOG_FATAL, path);
    (void)mode;
    (void)verbosity;
#elif LOGGING_HAS_SPDLOG
    {
        spdlog_backend::ensure_logger();
        const bool truncate  = (mode == logger::file_mode::truncate);
        auto       file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, truncate);
        file_sink->set_level(spdlog_backend::to_spdlog_min_level(verbosity));
        file_sink->set_pattern("[%l] %s:%# %v");
        const std::scoped_lock guard(spdlog_backend::g_sinks_mutex);
        spdlog_backend::g_file_sinks[path] = file_sink;
        spdlog_backend::g_dist_sink->add_sink(file_sink);
    }
#elif LOGGING_HAS_NATIVE
    internal::native_log_to_file(path, mode, verbosity);
#else
    (void)path;
    (void)mode;
    (void)verbosity;
#endif
}

void logger::end_log_to_file(const char* path)
{
#if LOGGING_HAS_LOGURU
    if (path != nullptr)
    {
        loguru::remove_callback(path);
    }
#elif LOGGING_HAS_GLOG
    google::FlushLogFiles(google::GLOG_INFO);
    g_glog_has_file = false;
    apply_console_sinks();
    (void)path;
#elif LOGGING_HAS_SPDLOG
    {
        if (path == nullptr)
        {
            return;
        }
        const std::scoped_lock guard(spdlog_backend::g_sinks_mutex);
        auto                   it = spdlog_backend::g_file_sinks.find(path);
        if (it != spdlog_backend::g_file_sinks.end())
        {
            it->second->flush();
            spdlog_backend::g_dist_sink->remove_sink(it->second);
            spdlog_backend::g_file_sinks.erase(it);
        }
    }
#elif LOGGING_HAS_NATIVE
    internal::native_end_log_to_file(path);
#else
    (void)path;
#endif
}

void logger::flush()
{
#if LOGGING_HAS_LOGURU
    loguru::flush();
#elif LOGGING_HAS_GLOG
    google::FlushLogFiles(google::GLOG_INFO);
#elif LOGGING_HAS_SPDLOG
    spdlog_backend::ensure_logger();
    spdlog_backend::g_logger->flush();
#elif LOGGING_HAS_NATIVE
    internal::native_flush();
#endif
}

void logger::set_thread_name(const std::string& name)
{
#if LOGGING_HAS_LOGURU
    loguru::set_thread_name(name.c_str());
    copy_thread_name(detail::ThreadName, sizeof(detail::ThreadName), name);
#elif LOGGING_HAS_GLOG
    copy_thread_name(detail::ThreadName, sizeof(detail::ThreadName), name);
#elif LOGGING_HAS_NATIVE
    copy_thread_name(internal::ThreadName, sizeof(internal::ThreadName), name);
#elif LOGGING_HAS_SPDLOG
    copy_thread_name(spdlog_backend::ThreadName, sizeof(spdlog_backend::ThreadName), name);
    spdlog_backend::ensure_logger();
    if (spdlog_backend::ThreadName[0] != '\0')
    {
        spdlog_backend::g_logger->set_pattern(
            fmt::format("[%^%l%$] [{}] %s:%# %v", spdlog_backend::ThreadName));
    }
#else
    copy_thread_name(detail::ThreadName, sizeof(detail::ThreadName), name);
#endif
}

std::string logger::get_thread_name()
{
#if LOGGING_HAS_LOGURU
    if (std::strlen(detail::ThreadName) > 0)
    {
        return {detail::ThreadName};
    }
    char buffer[128];
    loguru::get_thread_name(buffer, 128, false);
    return {buffer};
#elif LOGGING_HAS_GLOG
    if (std::strlen(detail::ThreadName) > 0)
    {
        return {detail::ThreadName};
    }
    return {"N/A"};
#elif LOGGING_HAS_NATIVE
    if (std::strlen(internal::ThreadName) > 0)
    {
        return {internal::ThreadName};
    }
    return {"N/A"};
#elif LOGGING_HAS_SPDLOG
    if (std::strlen(spdlog_backend::ThreadName) > 0)
    {
        return {spdlog_backend::ThreadName};
    }
    return {"N/A"};
#else
    if (std::strlen(detail::ThreadName) > 0)
    {
        return {detail::ThreadName};
    }
    return {"N/A"};
#endif
}

namespace
{
#if LOGGING_HAS_LOGURU
struct CallbackBridgeData
{
    logger::log_handler_callback_t   handler{nullptr};
    logger::close_handler_callback_t close{nullptr};
    logger::flush_handler_callback_t flush{nullptr};
    void*                            inner_data{nullptr};
};

void loguru_callback_bridge_handler(void* user_data, const loguru::Message& message)
{
    auto* data = reinterpret_cast<CallbackBridgeData*>(user_data);

    logger::Message logging_message;
    logging_message.verbosity   = static_cast<logger_verbosity_enum>(message.verbosity);
    logging_message.filename    = message.filename ? message.filename : "";
    logging_message.line        = message.line;
    logging_message.preamble    = message.preamble ? message.preamble : "";
    logging_message.indentation = message.indentation ? message.indentation : "";
    logging_message.prefix      = message.prefix ? message.prefix : "";
    logging_message.message     = message.message ? message.message : "";

    data->handler(data->inner_data, logging_message);
}

void loguru_callback_bridge_close(void* user_data)
{
    auto* data = reinterpret_cast<CallbackBridgeData*>(user_data);
    if (data->close != nullptr)
    {
        data->close(data->inner_data);
        data->inner_data = nullptr;
    }
    delete data;
}

void loguru_callback_bridge_flush(void* user_data)
{
    auto* data = reinterpret_cast<CallbackBridgeData*>(user_data);
    if (data->flush != nullptr)
    {
        data->flush(data->inner_data);
    }
}
#endif
}  // namespace

void logger::add_callback(
    const char*                      id,
    logger::log_handler_callback_t   callback,
    void*                            user_data,
    logger_verbosity_enum            verbosity,
    logger::close_handler_callback_t on_close,
    logger::flush_handler_callback_t on_flush)
{
#if LOGGING_HAS_LOGURU
    auto* callback_data = new CallbackBridgeData{callback, on_close, on_flush, user_data};
    loguru::add_callback(
        id,
        loguru_callback_bridge_handler,
        callback_data,
        static_cast<loguru::Verbosity>(verbosity),
        loguru_callback_bridge_close,
        loguru_callback_bridge_flush);
#elif LOGGING_HAS_SPDLOG
    {
        spdlog_backend::ensure_logger();
        auto cb_sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [callback, user_data](const spdlog::details::log_msg& msg)
            {
                logger::Message logging_msg;
                logging_msg.filename = msg.source.filename ? msg.source.filename : "";
                logging_msg.message  = std::string(msg.payload.data(), msg.payload.size());
                logging_msg.preamble = fmt::format(
                    "[{}] {}:{}",
                    spdlog::level::to_string_view(msg.level),
                    logging_msg.filename,
                    msg.source.line);
                logging_msg.verbosity = spdlog_backend::from_spdlog_level(msg.level);
                logging_msg.line      = static_cast<unsigned>(msg.source.line);
                callback(user_data, logging_msg);
            });
        cb_sink->set_level(spdlog_backend::to_spdlog_min_level(verbosity));

        const std::scoped_lock guard(spdlog_backend::g_sinks_mutex);
        spdlog_backend::g_callback_sinks[id] =
            spdlog_backend::CallbackEntry{cb_sink, on_close, on_flush, user_data};
        spdlog_backend::g_dist_sink->add_sink(cb_sink);
    }
#elif LOGGING_HAS_NATIVE
    internal::native_add_callback(id, callback, user_data, verbosity, on_close, on_flush);
#elif LOGGING_HAS_GLOG
    (void)id;
    (void)callback;
    (void)user_data;
    (void)verbosity;
    (void)on_close;
    (void)on_flush;
#else
    (void)id;
    (void)callback;
    (void)user_data;
    (void)verbosity;
    (void)on_close;
    (void)on_flush;
#endif
}

bool logger::remove_callback(const char* id)
{
#if LOGGING_HAS_LOGURU
    return loguru::remove_callback(id);
#elif LOGGING_HAS_SPDLOG
    {
        const std::scoped_lock guard(spdlog_backend::g_sinks_mutex);
        auto                   it = spdlog_backend::g_callback_sinks.find(id);
        if (it == spdlog_backend::g_callback_sinks.end())
        {
            return false;
        }
        spdlog_backend::g_dist_sink->remove_sink(it->second.sink);
        if (it->second.on_close != nullptr)
        {
            it->second.on_close(it->second.user_data);
        }
        spdlog_backend::g_callback_sinks.erase(it);
        return true;
    }
#elif LOGGING_HAS_NATIVE
    return internal::native_remove_callback(id);
#else
    (void)id;
    return false;
#endif
}

bool logger::is_enabled()
{
#if LOGGING_HAS_LOGURU || LOGGING_HAS_GLOG || LOGGING_HAS_NATIVE || LOGGING_HAS_SPDLOG
    return true;
#else
    return false;
#endif
}

logger_verbosity_enum logger::get_current_verbosity_cutoff()
{
#if LOGGING_HAS_LOGURU
    return static_cast<logger_verbosity_enum>(loguru::current_verbosity_cutoff());
#elif LOGGING_HAS_GLOG
    if (FLAGS_v > 0)
    {
        return logger::convert_to_verbosity(FLAGS_v);
    }
    if (FLAGS_minloglevel >= google::GLOG_FATAL + 1)
    {
        return logger_verbosity_enum::VERBOSITY_OFF;
    }
    if (FLAGS_minloglevel >= google::GLOG_FATAL)
    {
        return logger_verbosity_enum::VERBOSITY_FATAL;
    }
    if (FLAGS_minloglevel >= google::GLOG_ERROR)
    {
        return logger_verbosity_enum::VERBOSITY_ERROR;
    }
    if (FLAGS_minloglevel >= google::GLOG_WARNING)
    {
        return logger_verbosity_enum::VERBOSITY_WARNING;
    }
    return logger_verbosity_enum::VERBOSITY_INFO;
#elif LOGGING_HAS_NATIVE
    return static_cast<logger_verbosity_enum>(internal::native_cutoff());
#elif LOGGING_HAS_SPDLOG
    spdlog_backend::ensure_logger();
    return spdlog_backend::from_spdlog_level(spdlog_backend::g_logger->level());
#else
    return logger_verbosity_enum::VERBOSITY_INVALID;
#endif
}

void logger::log(
    logger_verbosity_enum verbosity, const char* fname, unsigned int lineno, const char* txt)
{
#if LOGGING_HAS_LOGURU || LOGGING_HAS_GLOG || LOGGING_HAS_NATIVE || LOGGING_HAS_SPDLOG
    const char* text = (txt != nullptr) ? txt : "";
#if LOGGING_HAS_LOGURU
    loguru::log(static_cast<loguru::Verbosity>(verbosity), fname, lineno, "%s", text);
    abort_if_fatal(verbosity);
#elif LOGGING_HAS_GLOG
    if (!google::IsGoogleLoggingInitialized())
    {
        google::InitGoogleLogging((fname != nullptr) ? fname : "logging");
        FLAGS_colorlogtostderr = true;
        apply_console_sinks();
    }
    {
        const char* file = (fname != nullptr) ? fname : "unknown";
        const int   line = static_cast<int>(lineno);
        if (verbosity == logger_verbosity_enum::VERBOSITY_ERROR)
        {
            google::LogMessage(file, line, google::GLOG_ERROR).stream() << text;
        }
        else if (verbosity == logger_verbosity_enum::VERBOSITY_WARNING)
        {
            google::LogMessage(file, line, google::GLOG_WARNING).stream() << text;
        }
        else if (verbosity == logger_verbosity_enum::VERBOSITY_INFO)
        {
            google::LogMessage(file, line, google::GLOG_INFO).stream() << text;
        }
        else if (verbosity > logger_verbosity_enum::VERBOSITY_INFO)
        {
            VLOG(static_cast<int>(verbosity)) << text;
        }
        else
        {
            google::LogMessageFatal(file, line).stream() << text;
        }
    }
#elif LOGGING_HAS_NATIVE
    internal::native_log_output(fname, lineno, verbosity, text);
#elif LOGGING_HAS_SPDLOG
    spdlog_backend::ensure_logger();
    spdlog_backend::g_logger->log(
        spdlog::source_loc{fname, static_cast<int>(lineno), ""},
        spdlog_backend::to_spdlog_msg_level(verbosity),
        "{}",
        text);
    abort_if_fatal(verbosity);
#else
    (void)verbosity;
    (void)fname;
    (void)lineno;
    (void)txt;
#endif
#else
    (void)verbosity;
    (void)fname;
    (void)lineno;
    (void)txt;
#endif
}

// NOLINTNEXTLINE(modernize-avoid-variadic-functions)
void logger::log_f(
    logger_verbosity_enum verbosity,
    const char*           fname,
    unsigned int          lineno,
    const char*           format,
    ...)
{
    va_list vlist;
    va_start(vlist, format);
    const std::string formatted = vformat_printf(format, vlist);
    va_end(vlist);
    logger::log(verbosity, fname, lineno, formatted.c_str());
}

void logger::start_scope(
    logger_verbosity_enum verbosity, const char* id, const char* fname, unsigned int lineno)
{
#if LOGGING_HAS_LOGURU
    detail::loguru_push_scope(
        id,
        verbosity > logger::get_current_verbosity_cutoff()
            ? std::make_shared<loguru::LogScopeRAII>()
            : std::make_shared<loguru::LogScopeRAII>(
                  static_cast<loguru::Verbosity>(verbosity), fname, lineno, "%s", id ? id : ""));
#else
    detail::push_named_scope(
        detail::named_scope{id ? id : "", verbosity, fname ? fname : "", lineno});
    logger::log(verbosity, fname, lineno, id);
#endif
}

void logger::end_scope(const char* id)
{
#if LOGGING_HAS_LOGURU
    detail::loguru_pop_scope(id);
#else
    detail::pop_named_scope(id);
#endif
}

// NOLINTNEXTLINE(modernize-avoid-variadic-functions)
void logger::start_scope_f(
    logger_verbosity_enum verbosity,
    const char*           id,
    const char*           fname,
    unsigned int          lineno,
    const char*           format,
    ...)
{
    va_list vlist;
    va_start(vlist, format);
    const std::string formatted = vformat_printf(format, vlist);
    va_end(vlist);
#if LOGGING_HAS_LOGURU
    if (verbosity > logger::get_current_verbosity_cutoff())
    {
        detail::loguru_push_scope(id, std::make_shared<loguru::LogScopeRAII>());
    }
    else
    {
        detail::loguru_push_scope(
            id,
            std::make_shared<loguru::LogScopeRAII>(
                static_cast<loguru::Verbosity>(verbosity), fname, lineno, "%s", formatted.c_str()));
    }
#else
    (void)id;
    logger::log(verbosity, fname, lineno, formatted.c_str());
#endif
}

logger_verbosity_enum logger::convert_to_verbosity(int value)
{
    if (value <= static_cast<int>(logger_verbosity_enum::VERBOSITY_INVALID))
    {
        return logger_verbosity_enum::VERBOSITY_INVALID;
    }
    if (value > static_cast<int>(logger_verbosity_enum::VERBOSITY_MAX))
    {
        return logger_verbosity_enum::VERBOSITY_MAX;
    }
    return static_cast<logger_verbosity_enum>(value);
}

logger_verbosity_enum logger::convert_to_verbosity(const char* text)
{
    if (text == nullptr)
    {
        return logger_verbosity_enum::VERBOSITY_INVALID;
    }
    char*     end    = nullptr;
    const int ivalue = static_cast<int>(std::strtol(text, &end, 10));
    if (end != text && *end == '\0')
    {
        return logger::convert_to_verbosity(ivalue);
    }

    std::string upper(text);
    std::transform(
        upper.begin(),
        upper.end(),
        upper.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (upper == "OFF")
    {
        return logger_verbosity_enum::VERBOSITY_OFF;
    }
    if (upper == "FATAL")
    {
        return logger_verbosity_enum::VERBOSITY_FATAL;
    }
    if (upper == "ERROR")
    {
        return logger_verbosity_enum::VERBOSITY_ERROR;
    }
    if (upper == "WARNING" || upper == "WARN")
    {
        return logger_verbosity_enum::VERBOSITY_WARNING;
    }
    if (upper == "INFO")
    {
        return logger_verbosity_enum::VERBOSITY_INFO;
    }
    if (upper == "TRACE")
    {
        return logger_verbosity_enum::VERBOSITY_TRACE;
    }
    if (upper == "MAX")
    {
        return logger_verbosity_enum::VERBOSITY_MAX;
    }
    return logger_verbosity_enum::VERBOSITY_INVALID;
}
}  // namespace logging
