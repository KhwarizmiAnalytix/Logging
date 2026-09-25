#include "include/logging/backend.h"

#if LOGGING_HAS_SPDLOG

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/spdlog.h>

#include "src/backend/backend_shared.h"

namespace logging
{
namespace backend
{
namespace
{
using shared::abort_if_fatal;
using shared::CallbackReentrancyGuard;
using shared::copy_thread_name;
using shared::ensure_parent_directory;

spdlog::level::level_enum to_spdlog_min_level(logger_verbosity_enum v)
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

spdlog::level::level_enum to_spdlog_msg_level(logger_verbosity_enum v)
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

logger_verbosity_enum from_spdlog_level(spdlog::level::level_enum l)
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

struct CallbackEntry
{
    std::shared_ptr<spdlog::sinks::sink> sink;
    logger::close_handler_callback_t     on_close{nullptr};
    logger::flush_handler_callback_t     on_flush{nullptr};
    void*                                user_data{nullptr};
};

thread_local char g_thread_name[128] = {};

class SpdlogBackend : public Backend
{
public:
    void log(
        logger_verbosity_enum severity, const char* fname, unsigned line, const char* msg) override
    {
        ensure_logger();
        const char* text = (msg != nullptr) ? msg : "";
        logger_->log(spdlog::source_loc{fname, static_cast<int>(line), ""},
            to_spdlog_msg_level(severity),
            "{}",
            text);
        abort_if_fatal(severity);
    }

    logger_verbosity_enum get_cutoff() const override
    {
        const_cast<SpdlogBackend*>(this)->ensure_logger();
        // Most-verbose (lowest spdlog level) across every active sink. The
        // logger's own level stays permissive; each sink gates itself, so a
        // callback/file sink is not starved by a stricter stderr cutoff.
        const std::scoped_lock    guard(sinks_mutex_);
        spdlog::level::level_enum best = spdlog::level::off;
        if (console_mode_.load(std::memory_order_relaxed) && stderr_sink_ != nullptr)
        {
            best = std::min(best, stderr_sink_->level());
        }
        for (const auto& [path, sink] : file_sinks_)
        {
            (void)path;
            best = std::min(best, sink->level());
        }
        for (const auto& [id, entry] : callback_sinks_)
        {
            (void)id;
            best = std::min(best, entry.sink->level());
        }
        return from_spdlog_level(best);
    }

    void set_stderr_verbosity(logger_verbosity_enum severity) override
    {
        ensure_logger();
        requested_verbosity_ = severity;
        apply_stderr_level();
    }

    void set_internal_verbosity(logger_verbosity_enum severity) override
    {
        set_stderr_verbosity(severity);
    }

    void set_console_mode(bool enabled) override
    {
        console_mode_.store(enabled, std::memory_order_relaxed);
        ensure_logger();
        apply_stderr_level();
    }

    bool get_console_mode() const override { return console_mode_.load(std::memory_order_relaxed); }

    void log_to_file(
        const char* path, logger::file_mode mode, logger_verbosity_enum severity) override
    {
        if ((path == nullptr) || *path == '\0')
        {
            return;
        }
        ensure_parent_directory(path);
        ensure_logger();
        const bool truncate  = (mode == logger::file_mode::truncate);
        auto       file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, truncate);
        file_sink->set_level(to_spdlog_min_level(severity));
        file_sink->set_pattern("[%l] %s:%# %v");
        const std::scoped_lock guard(sinks_mutex_);
        file_sinks_[path] = file_sink;
        dist_sink_->add_sink(file_sink);
    }

    void end_log_to_file(const char* path) override
    {
        if (path == nullptr)
        {
            return;
        }
        const std::scoped_lock guard(sinks_mutex_);
        auto                   it = file_sinks_.find(path);
        if (it != file_sinks_.end())
        {
            it->second->flush();
            dist_sink_->remove_sink(it->second);
            file_sinks_.erase(it);
        }
    }

    void flush() override
    {
        ensure_logger();
        logger_->flush();

        // Fan out to registered on_flush handlers outside the lock (spdlog's
        // own flush does not know about them). Matches native/loguru behavior.
        std::vector<std::pair<logger::flush_handler_callback_t, void*>> callbacks_to_invoke;
        {
            const std::scoped_lock guard(sinks_mutex_);
            for (const auto& [id, entry] : callback_sinks_)
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
            CallbackReentrancyGuard guard;
            if (!guard.is_reentrant())
            {
                cb(user_data);
            }
        }
    }

    void set_thread_name(const std::string& name) override
    {
        copy_thread_name(g_thread_name, sizeof(g_thread_name), name);
        ensure_logger();
        if (g_thread_name[0] != '\0')
        {
            logger_->set_pattern(fmt::format("[%^%l%$] [{}] %s:%# %v", g_thread_name));
        }
    }

    std::string get_thread_name() const override
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
        logger::flush_handler_callback_t on_flush) override
    {
        if (id == nullptr)
        {
            return;
        }
        ensure_logger();
        auto cb_sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [callback, user_data](const spdlog::details::log_msg& msg)
            {
                logger::Message logging_msg;
                logging_msg.filename  = msg.source.filename ? msg.source.filename : "";
                logging_msg.message   = std::string(msg.payload.data(), msg.payload.size());
                logging_msg.preamble  = fmt::format("[{}] {}:{}",
                    spdlog::level::to_string_view(msg.level),
                    logging_msg.filename,
                    msg.source.line);
                logging_msg.verbosity = from_spdlog_level(msg.level);
                logging_msg.line      = static_cast<unsigned>(msg.source.line);
                CallbackReentrancyGuard guard;
                if (!guard.is_reentrant())
                {
                    callback(user_data, logging_msg);
                }
            });
        cb_sink->set_level(to_spdlog_min_level(severity));

        const std::scoped_lock guard(sinks_mutex_);
        callback_sinks_[id] = CallbackEntry{cb_sink, on_close, on_flush, user_data};
        dist_sink_->add_sink(cb_sink);
    }

    bool remove_callback(const char* id) override
    {
        logger::close_handler_callback_t on_close  = nullptr;
        void*                            user_data = nullptr;
        {
            const std::scoped_lock guard(sinks_mutex_);
            auto                   it = callback_sinks_.find(id);
            if (it == callback_sinks_.end())
            {
                return false;
            }
            dist_sink_->remove_sink(it->second.sink);
            on_close  = it->second.on_close;
            user_data = it->second.user_data;
            callback_sinks_.erase(it);
        }

        if (on_close != nullptr)
        {
            CallbackReentrancyGuard guard;
            if (!guard.is_reentrant())
            {
                on_close(user_data);
            }
        }
        return true;
    }

    std::unique_ptr<scope_state> scope_enter(logger_verbosity_enum severity,
        const char*                                                fname,
        unsigned                                                   line,
        const std::string&                                         msg) override
    {
        ensure_logger();
        logger_->log(spdlog::source_loc{fname, static_cast<int>(line), ""},
            to_spdlog_msg_level(severity),
            "[scope enter] {}",
            msg);
        return std::make_unique<SpdlogScope>(*this, severity, fname, line, msg);
    }

    void on_init(int& argc, char* argv[], const init_options& options) override
    {
        (void)argc;
        (void)argv;
        ensure_logger();
        if (options.main_thread_name != nullptr && options.main_thread_name[0] != '\0')
        {
            set_thread_name(options.main_thread_name);
        }
        set_console_mode(console_mode_.load(std::memory_order_relaxed));
    }

private:
    class SpdlogScope : public scope_state
    {
    public:
        SpdlogScope(SpdlogBackend& owner,
            logger_verbosity_enum  severity,
            const char*            fname,
            unsigned               line,
            std::string            msg)
            : owner_(owner), severity_(severity), fname_(fname != nullptr ? fname : ""),
              line_(line), msg_(std::move(msg)), entry_time_(std::chrono::steady_clock::now())
        {
        }

        ~SpdlogScope() override
        {
            const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - entry_time_)
                                        .count();
            owner_.ensure_logger();
            owner_.logger_->log(spdlog::source_loc{fname_.c_str(), static_cast<int>(line_), ""},
                to_spdlog_msg_level(severity_),
                "[scope exit]  {} ({} us)",
                msg_,
                elapsed_us);
        }

    private:
        SpdlogBackend&                        owner_;
        logger_verbosity_enum                 severity_;
        std::string                           fname_;
        unsigned                              line_;
        std::string                           msg_;
        std::chrono::steady_clock::time_point entry_time_;
    };

    void ensure_logger()
    {
        std::call_once(init_flag_,
            [this]()
            {
                dist_sink_       = std::make_shared<spdlog::sinks::dist_sink_mt>();
                auto stderr_sink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
                stderr_sink->set_pattern("%^[%l]%$ %s:%# %v");
                stderr_sink_ = stderr_sink;
                dist_sink_->add_sink(stderr_sink_);
                if (!console_mode_.load(std::memory_order_relaxed))
                {
                    stderr_sink_->set_level(spdlog::level::off);
                }
                logger_ = std::make_shared<spdlog::logger>("logging", dist_sink_);
                // Master level stays permissive; per-sink levels do the gating
                // so each destination has an independent cutoff.
                logger_->set_level(spdlog::level::trace);
                logger_->flush_on(spdlog::level::err);
                stderr_sink_->set_level(spdlog::level::info);
            });
    }

    // stderr sink level tracks the requested stderr verbosity, or off when the
    // console is disabled.
    void apply_stderr_level()
    {
        const std::scoped_lock guard(sinks_mutex_);
        if (stderr_sink_ != nullptr)
        {
            stderr_sink_->set_level(console_mode_.load(std::memory_order_relaxed)
                                        ? to_spdlog_min_level(requested_verbosity_)
                                        : spdlog::level::off);
        }
    }

    std::once_flag        init_flag_;
    mutable std::mutex    sinks_mutex_;
    std::atomic<bool>     console_mode_{true};
    logger_verbosity_enum requested_verbosity_{logger_verbosity_enum::VERBOSITY_INFO};
    std::shared_ptr<spdlog::sinks::dist_sink_mt>                          dist_sink_;
    std::shared_ptr<spdlog::logger>                                       logger_;
    std::shared_ptr<spdlog::sinks::sink>                                  stderr_sink_;
    std::unordered_map<std::string, std::shared_ptr<spdlog::sinks::sink>> file_sinks_;
    std::unordered_map<std::string, CallbackEntry>                        callback_sinks_;
};

}  // namespace

std::unique_ptr<Backend> create_spdlog_backend()
{
    return std::make_unique<SpdlogBackend>();
}

}  // namespace backend
}  // namespace logging

#endif  // LOGGING_HAS_SPDLOG
