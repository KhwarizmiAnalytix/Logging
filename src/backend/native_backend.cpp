#include "include/logging/backend.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/format.h>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "include/common/logging_macros.h"
#include "include/logger/logger.h"
#include "include/logger/logger_verbosity_enum.h"
#include "include/logging/level.h"

namespace logging
{
namespace backend
{
namespace
{

using logger_verbosity_enum = logging::logger_verbosity_enum;

std::atomic<int>  g_cutoff{static_cast<int>(logger_verbosity_enum::VERBOSITY_INFO)};
std::mutex        g_io_mutex;
std::atomic<bool> g_console_mode{true};

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

struct file_sink
{
    std::string   path;
    std::ofstream stream;
    int           verbosity{static_cast<int>(logger_verbosity_enum::VERBOSITY_INFO)};
};

std::vector<file_sink> g_files;

struct callback_entry
{
    void* callback{nullptr};
    void* on_close{nullptr};
    void* on_flush{nullptr};
    void* user_data{nullptr};
    int   verbosity{static_cast<int>(logger_verbosity_enum::VERBOSITY_INFO)};
};

std::unordered_map<std::string, callback_entry> g_callbacks;
thread_local char                               g_thread_name[128] = {};

const char* level_to_string(int verbosity_int)
{
    auto verbosity = static_cast<logger_verbosity_enum>(verbosity_int);
    switch (verbosity)
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

fmt::color get_severity_color(int verbosity_int)
{
    auto verbosity = static_cast<logger_verbosity_enum>(verbosity_int);
    switch (verbosity)
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

std::string format_line(
    const char* fname, unsigned lineno, int verbosity_int, const std::string& message)
{
    const char* thread = g_thread_name;
    if (thread[0] != '\0')
    {
        return fmt::format("[{}] [{}] {}:{} {}",
            level_to_string(verbosity_int),
            thread,
            basename_from_path(fname),
            lineno,
            message);
    }
    return fmt::format("[{}] {}:{} {}",
        level_to_string(verbosity_int),
        basename_from_path(fname),
        lineno,
        message);
}

void ensure_parent_directory(const char* path)
{
    if (path == nullptr)
    {
        return;
    }

    std::string dir(path);
    size_t      pos = dir.find_last_of("/\\");
    if (pos == std::string::npos)
    {
        return;
    }

    dir = dir.substr(0, pos);
    if (dir.empty())
    {
        return;
    }

    std::filesystem::create_directories(dir);

}

void abort_if_fatal(int verbosity_int)
{
    if (verbosity_int == static_cast<int>(logger_verbosity_enum::VERBOSITY_FATAL))
    {
        std::abort();
    }
}

}  // namespace

class NativeBackend : public Backend
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

void NativeBackend::log(int verbosity, const char* fname, unsigned int lineno, const char* message)
{
    if (message == nullptr ||
        (message[0] == '\0' &&
            verbosity != static_cast<int>(logger_verbosity_enum::VERBOSITY_FATAL)))
    {
        return;
    }

    const int cutoff = g_cutoff.load(std::memory_order_relaxed);
    if (verbosity > cutoff && verbosity != static_cast<int>(logger_verbosity_enum::VERBOSITY_FATAL))
    {
        return;
    }

    const std::string line = format_line(fname, lineno, verbosity, message);

    logger::Message payload;
    payload.verbosity = static_cast<logger_verbosity_enum>(verbosity);
    payload.filename  = basename_from_path(fname);
    payload.line      = lineno;
    payload.preamble  = line;
    payload.message   = message;

    std::vector<callback_entry> callbacks_copy;
    {
        const std::scoped_lock guard(g_io_mutex);
        if (g_console_mode.load(std::memory_order_relaxed) ||
            verbosity == static_cast<int>(logger_verbosity_enum::VERBOSITY_FATAL))
        {
            fmt::print(stderr, fg(get_severity_color(verbosity)), "{}\n", line);
        }

        for (auto& sink : g_files)
        {
            if (verbosity <= sink.verbosity && sink.stream.is_open())
            {
                sink.stream << line << '\n';
            }
        }

        callbacks_copy.reserve(g_callbacks.size());
        for (const auto& [id, entry] : g_callbacks)
        {
            (void)id;
            if (entry.callback != nullptr && verbosity <= entry.verbosity)
            {
                callbacks_copy.push_back(entry);
            }
        }
    }

    for (const auto& entry : callbacks_copy)
    {
        auto callback = reinterpret_cast<void (*)(void*, const logger::Message&)>(entry.callback);
        callback(entry.user_data, payload);
    }

    abort_if_fatal(verbosity);
}

void NativeBackend::set_cutoff(int verbosity)
{
    g_cutoff.store(verbosity, std::memory_order_relaxed);
}

int NativeBackend::get_cutoff() const
{
    return g_cutoff.load(std::memory_order_relaxed);
}

void NativeBackend::log_to_file(const char* path, bool truncate, int verbosity)
{
    if (path == nullptr || *path == '\0')
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
    sink.path      = path;
    sink.verbosity = verbosity;
    const auto open_mode =
        truncate ? (std::ios::out | std::ios::trunc) : (std::ios::out | std::ios::app);
    sink.stream.open(path, open_mode);
    g_files.push_back(std::move(sink));
}

void NativeBackend::end_log_to_file(const char* path)
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

void NativeBackend::set_console_mode(bool enabled)
{
    g_console_mode.store(enabled, std::memory_order_relaxed);
}

bool NativeBackend::get_console_mode() const
{
    return g_console_mode.load(std::memory_order_relaxed);
}

void NativeBackend::add_callback(const char* id,
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
    const std::scoped_lock guard(g_io_mutex);
    g_callbacks[id] =
        callback_entry{log_handler_ptr, on_close_ptr, on_flush_ptr, user_data, verbosity};
}

bool NativeBackend::remove_callback(const char* id)
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
        auto on_close = reinterpret_cast<void (*)(void*)>(it->second.on_close);
        on_close(it->second.user_data);
    }
    g_callbacks.erase(it);
    return true;
}

void NativeBackend::flush()
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
            auto on_flush = reinterpret_cast<void (*)(void*)>(entry.on_flush);
            on_flush(entry.user_data);
        }
    }
}

void NativeBackend::shutdown()
{
    {
        const std::scoped_lock guard(g_io_mutex);
        g_files.clear();
        g_callbacks.clear();
    }
}

void NativeBackend::set_thread_name(std::string_view name)
{
    const size_t copy_size = std::min(name.size(), sizeof(g_thread_name) - 1);
    if (copy_size > 0)
    {
        std::memcpy(g_thread_name, name.data(), copy_size);
    }
    g_thread_name[copy_size] = '\0';
}

std::unique_ptr<Backend> create_native_backend()
{
    return std::make_unique<NativeBackend>();
}

}  // namespace backend
}  // namespace logging
