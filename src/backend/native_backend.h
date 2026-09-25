#ifndef LOGGING_SRC_BACKEND_NATIVE_BACKEND_H
#define LOGGING_SRC_BACKEND_NATIVE_BACKEND_H

#include "include/logger/logger.h"

#if LOGGING_HAS_NATIVE

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/color.h>
#include <fmt/format.h>

#include "src/backend/backend_shared.h"
#include "src/backend/backend_types.h"

namespace logging
{
namespace backend
{
namespace detail
{

inline const char* verbosity_to_string(logger_verbosity_enum severity)
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

inline fmt::color get_severity_color(logger_verbosity_enum severity)
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

// Concrete backend, selected at compile time (see backend.h). No virtual
// methods: this is the only backend type in the binary, so every call here
// is an ordinary, statically-dispatched (and inlinable) member function call.
class NativeBackend
{
public:
    void log(logger_verbosity_enum severity, const char* fname, unsigned line, const char* msg)
    {
        const std::string message = (msg != nullptr) ? msg : "";
        if (message.empty() && severity != logger_verbosity_enum::VERBOSITY_FATAL)
        {
            return;
        }

        const bool fatal         = (severity == logger_verbosity_enum::VERBOSITY_FATAL);
        const int  stderr_cutoff = cutoff_.load(std::memory_order_relaxed);
        if (!fatal && static_cast<int>(severity) > effective_cutoff())
        {
            return;
        }

        const std::string formatted_line = format_line(fname, line, severity, message);

        logger::Message payload;
        payload.verbosity = severity;
        payload.filename  = shared::basename_from_path(fname);
        payload.line      = line;
        payload.preamble  = formatted_line;
        payload.message   = message;

        std::vector<callback_entry> callbacks_copy;
        {
            const std::scoped_lock guard(io_mutex_);
            if (fatal ||
                (console_mode_.load(std::memory_order_relaxed) &&
                    static_cast<int>(severity) <= stderr_cutoff))
            {
                fmt::print(stderr, fg(get_severity_color(severity)), "{}\n", formatted_line);
            }

            for (auto& sink : files_)
            {
                if (severity <= sink.verbosity && sink.stream.is_open())
                {
                    sink.stream << formatted_line << '\n';
                }
            }

            callbacks_copy.reserve(callbacks_.size());
            for (const auto& [id, entry] : callbacks_)
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
            shared::CallbackReentrancyGuard guard;
            if (!guard.is_reentrant())
            {
                entry.callback(entry.user_data, payload);
            }
        }

        shared::abort_if_fatal(severity);
    }

    logger_verbosity_enum get_cutoff() const
    {
        return static_cast<logger_verbosity_enum>(effective_cutoff());
    }

    void set_stderr_verbosity(logger_verbosity_enum severity)
    {
        cutoff_.store(static_cast<int>(severity), std::memory_order_relaxed);
    }

    void set_internal_verbosity(logger_verbosity_enum severity)
    {
        cutoff_.store(static_cast<int>(severity), std::memory_order_relaxed);
    }

    void set_console_mode(bool enabled) { console_mode_.store(enabled, std::memory_order_relaxed); }

    bool get_console_mode() const { return console_mode_.load(std::memory_order_relaxed); }

    void log_to_file(const char* path, logger::file_mode mode, logger_verbosity_enum severity)
    {
        if ((path == nullptr) || *path == '\0')
        {
            return;
        }
        shared::ensure_parent_directory(path);
        const std::scoped_lock guard(io_mutex_);
        for (auto& sink : files_)
        {
            if (sink.path == path)
            {
                sink.verbosity = severity;
                return;
            }
        }
        file_sink sink;
        sink.path             = path;
        sink.verbosity        = severity;
        const auto open_mode = (mode == logger::file_mode::append) ? (std::ios::out | std::ios::app)
                                                                    : (std::ios::out | std::ios::trunc);
        sink.stream.open(path, open_mode);
        files_.push_back(std::move(sink));
    }

    void end_log_to_file(const char* path)
    {
        if (path == nullptr)
        {
            return;
        }
        const std::scoped_lock guard(io_mutex_);
        for (auto it = files_.begin(); it != files_.end(); ++it)
        {
            if (it->path == path)
            {
                if (it->stream.is_open())
                {
                    it->stream.flush();
                    it->stream.close();
                }
                files_.erase(it);
                return;
            }
        }
    }

    void flush()
    {
        std::vector<std::pair<logger::flush_handler_callback_t, void*>> callbacks_to_invoke;
        {
            const std::scoped_lock guard(io_mutex_);
            std::fflush(stderr);
            for (auto& sink : files_)
            {
                if (sink.stream.is_open())
                {
                    sink.stream.flush();
                }
            }
            for (auto& [id, entry] : callbacks_)
            {
                (void)id;
                if (entry.on_flush != nullptr)
                {
                    callbacks_to_invoke.push_back({entry.on_flush, entry.user_data});
                }
            }
        }

        for (auto& [cb, user_data] : callbacks_to_invoke)
        {
            shared::CallbackReentrancyGuard guard;
            if (!guard.is_reentrant())
            {
                cb(user_data);
            }
        }
    }

    void set_thread_name(const std::string& name)
    {
        shared::copy_thread_name(g_thread_name, sizeof(g_thread_name), name);
    }

    std::string get_thread_name() const
    {
        if (std::strlen(g_thread_name) > 0)
        {
            return {g_thread_name};
        }
        return {"N/A"};
    }

    void add_callback(const char*        id,
        logger::log_handler_callback_t   callback,
        void*                            user_data,
        logger_verbosity_enum            severity,
        logger::close_handler_callback_t on_close,
        logger::flush_handler_callback_t on_flush)
    {
        if (id == nullptr)
        {
            return;
        }
        const std::scoped_lock guard(io_mutex_);
        callbacks_[id] = callback_entry{callback, on_close, on_flush, user_data, severity};
    }

    bool remove_callback(const char* id)
    {
        if (id == nullptr)
        {
            return false;
        }
        logger::close_handler_callback_t on_close  = nullptr;
        void*                            user_data = nullptr;
        {
            const std::scoped_lock guard(io_mutex_);
            auto                   it = callbacks_.find(id);
            if (it == callbacks_.end())
            {
                return false;
            }
            on_close  = it->second.on_close;
            user_data = it->second.user_data;
            callbacks_.erase(it);
        }

        if (on_close != nullptr)
        {
            shared::CallbackReentrancyGuard guard;
            if (!guard.is_reentrant())
            {
                on_close(user_data);
            }
        }
        return true;
    }

    // RAII scope token. Since NativeBackend is the only backend type compiled
    // in, callers can hold this concrete type directly (no unique_ptr<base>).
    class Scope
    {
    public:
        Scope(NativeBackend&    owner,
            logger_verbosity_enum severity,
            const char*            fname,
            unsigned               line,
            std::string            msg)
            : owner_(&owner), severity_(severity), fname_(fname != nullptr ? fname : ""),
              line_(line), msg_(std::move(msg)), entry_time_(std::chrono::steady_clock::now())
        {
        }

        // Non-movable, non-copyable: always held via unique_ptr, so the raw
        // owner_ pointer never needs moved-from nulling.
        Scope(Scope&&)                 = delete;
        Scope& operator=(Scope&&)      = delete;
        Scope(const Scope&)            = delete;
        Scope& operator=(const Scope&) = delete;

        ~Scope()
        {
            if (owner_ == nullptr)
            {
                return;
            }
            const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - entry_time_)
                                        .count();
            const std::string text = fmt::format("[scope exit]  {} ({} us)", msg_, elapsed_us);
            owner_->log(severity_, fname_.c_str(), line_, text.c_str());
        }

    private:
        NativeBackend*                         owner_;
        logger_verbosity_enum                  severity_;
        std::string                            fname_;
        unsigned                                line_;
        std::string                            msg_;
        std::chrono::steady_clock::time_point  entry_time_;
    };

    std::unique_ptr<Scope> scope_enter(
        logger_verbosity_enum severity, const char* fname, unsigned line, const std::string& msg)
    {
        log(severity, fname, line, ("[scope enter] " + msg).c_str());
        return std::make_unique<Scope>(*this, severity, fname, line, msg);
    }

    void on_init(int& argc, char* argv[], const init_options& options)
    {
        (void)argc;
        (void)argv;
        (void)options;
    }

private:
    struct file_sink
    {
        std::string           path;
        std::ofstream         stream;
        logger_verbosity_enum verbosity{logger_verbosity_enum::VERBOSITY_INFO};
    };

    struct callback_entry
    {
        logger::log_handler_callback_t   callback{nullptr};
        logger::close_handler_callback_t on_close{nullptr};
        logger::flush_handler_callback_t on_flush{nullptr};
        void*                            user_data{nullptr};
        logger_verbosity_enum            verbosity{logger_verbosity_enum::VERBOSITY_INFO};
    };

    static std::string format_line(
        const char* fname, unsigned line, logger_verbosity_enum severity, const std::string& message)
    {
        const char* thread = g_thread_name;
        if (thread[0] != '\0')
        {
            return fmt::format("[{}] [{}] {}:{} {}",
                verbosity_to_string(severity),
                thread,
                shared::basename_from_path(fname),
                line,
                message);
        }
        return fmt::format("[{}] {}:{} {}",
            verbosity_to_string(severity),
            shared::basename_from_path(fname),
            line,
            message);
    }

    // Most-verbose (numerically largest, in loguru numbering) level any active
    // destination accepts: the stderr cutoff when the console is on, plus every
    // file and callback sink. Floored at FATAL so fatal records are never gated.
    int effective_cutoff() const
    {
        int cutoff = static_cast<int>(logger_verbosity_enum::VERBOSITY_FATAL);
        if (console_mode_.load(std::memory_order_relaxed))
        {
            cutoff = std::max(cutoff, cutoff_.load(std::memory_order_relaxed));
        }
        const std::scoped_lock guard(io_mutex_);
        for (const auto& sink : files_)
        {
            cutoff = std::max(cutoff, static_cast<int>(sink.verbosity));
        }
        for (const auto& [id, entry] : callbacks_)
        {
            (void)id;
            if (entry.callback != nullptr)
            {
                cutoff = std::max(cutoff, static_cast<int>(entry.verbosity));
            }
        }
        return cutoff;
    }

    static inline thread_local char g_thread_name[128] = {};

    std::atomic<int>   cutoff_{static_cast<int>(logger_verbosity_enum::VERBOSITY_INFO)};
    std::atomic<bool>  console_mode_{true};
    mutable std::mutex io_mutex_;
    std::vector<file_sink>                          files_;
    std::unordered_map<std::string, callback_entry> callbacks_;
};

}  // namespace detail
}  // namespace backend
}  // namespace logging

#endif  // LOGGING_HAS_NATIVE
#endif  // LOGGING_SRC_BACKEND_NATIVE_BACKEND_H
