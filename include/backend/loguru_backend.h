#ifndef LOGGING_INCLUDE_BACKEND_LOGURU_BACKEND_H
#define LOGGING_INCLUDE_BACKEND_LOGURU_BACKEND_H

#include "include/logger/logger.h"

#if LOGGING_HAS_LOGURU

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include <loguru.hpp>

#include "include/backend/backend_shared.h"
#include "include/backend/backend_types.h"

namespace logging
{
namespace backend
{
namespace detail
{

// Concrete backend, selected at compile time (see backend.h). No virtual
// methods: this is the only backend type in the binary, so every call here
// is an ordinary, staticly-dispatched (and inlinable) member function call.
class LoguruBackend
{
public:
    void log(logger_verbosity_enum severity, const char* fname, unsigned line, const char* msg)
    {
        const char* text = (msg != nullptr) ? msg : "";
        loguru::log(static_cast<loguru::Verbosity>(severity), fname, line, "%s", text);
        shared::abort_if_fatal(severity);
    }

    logger_verbosity_enum get_cutoff() const
    {
        return static_cast<logger_verbosity_enum>(loguru::current_verbosity_cutoff());
    }

    void set_stderr_verbosity(logger_verbosity_enum severity)
    {
        requested_verbosity_ = severity;
        apply_console();
    }

    void set_internal_verbosity(logger_verbosity_enum severity)
    {
        internal_verbosity_          = severity;
        loguru::g_internal_verbosity = static_cast<loguru::Verbosity>(severity);
    }

    void set_console_mode(bool enabled)
    {
        console_mode_.store(enabled, std::memory_order_relaxed);
        apply_console();
    }

    bool get_console_mode() const { return console_mode_.load(std::memory_order_relaxed); }

    void log_to_file(const char* path, logger::file_mode mode, logger_verbosity_enum severity)
    {
        if ((path == nullptr) || *path == '\0')
        {
            return;
        }
        shared::ensure_parent_directory(path);
        const loguru::FileMode loguru_mode =
            (mode == logger::file_mode::append) ? loguru::Append : loguru::Truncate;

        // loguru::add_file registers the file sink as a callback keyed by
        // `path` via the *raw* loguru::add_callback -- it does not go through
        // our add_callback() wrapper's duplicate-id tracking, so a second
        // log_to_file() call on the same path would accumulate a second open
        // file handle instead of replacing the first (mirrors the callback
        // duplicate-id gap fixed in add_callback; paths and callback ids
        // share the same loguru::s_callbacks id namespace).
        {
            const std::scoped_lock guard(ids_mutex_);
            if (registered_ids_.count(path) != 0)
            {
                loguru::remove_callback(path);
            }
            else
            {
                registered_ids_.insert(path);
            }
        }
        loguru::add_file(path, loguru_mode, static_cast<loguru::Verbosity>(severity));
    }

    void end_log_to_file(const char* path)
    {
        if (path == nullptr)
        {
            return;
        }
        {
            const std::scoped_lock guard(ids_mutex_);
            registered_ids_.erase(path);
        }
        loguru::remove_callback(path);
    }

    void flush() { loguru::flush(); }

    void set_thread_name(const std::string& name)
    {
        loguru::set_thread_name(name.c_str());
        shared::copy_thread_name(g_thread_name, sizeof(g_thread_name), name);
    }

    std::string get_thread_name() const
    {
        if (std::strlen(g_thread_name) > 0)
        {
            return {g_thread_name};
        }
        char buffer[128];
        loguru::get_thread_name(buffer, 128, false);
        return {buffer};
    }

    void add_callback(const char*        id,
        logger::log_handler_callback_t   callback,
        void*                            user_data,
        logger_verbosity_enum            severity,
        logger::close_handler_callback_t on_close,
        logger::flush_handler_callback_t on_flush)
    {
        // loguru::add_callback appends unconditionally (its s_callbacks is a
        // plain vector, no id uniqueness check), so a duplicate id would
        // accumulate a second live entry instead of replacing the first --
        // unlike native/spdlog. Remove any existing registration first so a
        // re-registration under the same id has the same replace semantics
        // on every backend. loguru::remove_callback takes the same
        // recursive mutex that guards log()'s callback dispatch, so this is
        // already race-free against an in-flight invocation. We track ids
        // ourselves so we only call remove_callback when there is actually
        // something to replace -- calling it on an unknown id logs a
        // spurious ERROR (loguru's own behavior).
        {
            const std::scoped_lock guard(ids_mutex_);
            if (registered_ids_.count(id) != 0)
            {
                loguru::remove_callback(id);
            }
            else
            {
                registered_ids_.insert(id);
            }
        }

        auto* callback_data = new CallbackBridgeData{callback, on_close, on_flush, user_data};
        loguru::add_callback(id,
            callback_bridge_handler,
            callback_data,
            static_cast<loguru::Verbosity>(severity),
            callback_bridge_close,
            callback_bridge_flush);
    }

    bool remove_callback(const char* id)
    {
        {
            const std::scoped_lock guard(ids_mutex_);
            registered_ids_.erase(id);
        }
        return loguru::remove_callback(id);
    }

    // RAII scope token. Since LoguruBackend is the only backend type compiled
    // in, callers can hold this concrete type directly (no unique_ptr<base>).
    class Scope
    {
    public:
        Scope(logger_verbosity_enum severity,
            const char*             fname,
            unsigned                line,
            const std::string&      msg)
            : data_(std::make_unique<loguru::LogScopeRAII>(
                  static_cast<loguru::Verbosity>(severity), fname, line, "%s", msg.c_str()))
        {
        }

    private:
        std::unique_ptr<loguru::LogScopeRAII> data_;
    };

    std::unique_ptr<Scope> scope_enter(
        logger_verbosity_enum severity, const char* fname, unsigned line, const std::string& msg)
    {
        return std::make_unique<Scope>(severity, fname, line, msg);
    }

    void on_init(int& argc, char* argv[], const init_options& options)
    {
        loguru::g_preamble_date      = false;
        loguru::g_preamble_time      = false;
        loguru::g_colorlogtostderr   = options.color;
        loguru::g_internal_verbosity = static_cast<loguru::Verbosity>(internal_verbosity_);

        const auto current_stderr_verbosity = loguru::g_stderr_verbosity;
        if (loguru::g_internal_verbosity > loguru::g_stderr_verbosity)
        {
            loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
        }

        loguru::Options loguru_options;
        loguru_options.verbosity_flag                       = options.verbosity_flag;
        loguru_options.signal_options.unsafe_signal_handler = options.unsafe_signal_handler;
        loguru_options.signal_options.sigabrt               = options.sigabrt;
        loguru_options.signal_options.sigbus                = options.sigbus;
        loguru_options.signal_options.sigfpe                = options.sigfpe;
        loguru_options.signal_options.sigill                = options.sigill;
        loguru_options.signal_options.sigint                = options.sigint;
        loguru_options.signal_options.sigsegv               = options.sigsegv;
        loguru_options.signal_options.sigterm               = options.sigterm;
        if (options.main_thread_name != nullptr && options.main_thread_name[0] != '\0')
        {
            loguru_options.main_thread_name = options.main_thread_name;
        }
        color_mode_ = options.color;
        loguru::init(argc, argv, loguru_options);
        loguru::g_stderr_verbosity = current_stderr_verbosity;
        apply_console();
    }

private:
    // Bridge from loguru's callback ABI to the library's typed callbacks. Heap
    // allocated per registration and freed by the loguru close bridge.
    struct CallbackBridgeData
    {
        logger::log_handler_callback_t   handler{nullptr};
        logger::close_handler_callback_t close{nullptr};
        logger::flush_handler_callback_t flush{nullptr};
        void*                            inner_data{nullptr};
    };

    static void callback_bridge_handler(void* user_data, const loguru::Message& message)
    {
        auto* data = static_cast<CallbackBridgeData*>(user_data);

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

    static void callback_bridge_close(void* user_data)
    {
        auto* data = static_cast<CallbackBridgeData*>(user_data);
        if (data->close != nullptr)
        {
            data->close(data->inner_data);
            data->inner_data = nullptr;
        }
        delete data;
    }

    static void callback_bridge_flush(void* user_data)
    {
        auto* data = static_cast<CallbackBridgeData*>(user_data);
        if (data->flush != nullptr)
        {
            data->flush(data->inner_data);
        }
    }

    // Writes ANSI-colored output directly to stderr, bypassing loguru's own
    // isatty-gated color path. Used when color_mode_ is true so that colored
    // output works even when stderr is not a TTY (e.g. under CTest).
    static void color_stderr_callback(void* /*user_data*/, const loguru::Message& message)
    {
        const char* color;
        const char* reset = "\033[0m";
        const auto  v     = message.verbosity;
        if (v == loguru::Verbosity_FATAL)
        {
            color = "\033[1;31m";  // bold red
        }
        else if (v == loguru::Verbosity_ERROR)
        {
            color = "\033[31m";  // red
        }
        else if (v == loguru::Verbosity_WARNING)
        {
            color = "\033[33m";  // yellow
        }
        else
        {
            color = "";  // default for INFO and below
        }
        std::fprintf(stderr,
            "%s%s%s%s%s%s\n",
            color,
            message.preamble    ? message.preamble    : "",
            message.indentation ? message.indentation : "",
            message.prefix      ? message.prefix      : "",
            message.message     ? message.message     : "",
            reset);
        std::fflush(stderr);
    }

    void apply_console()
    {
        const bool enabled = console_mode_.load(std::memory_order_relaxed);
#if defined(_WIN32)
        if (enabled)
        {
            shared::ensure_windows_console();
        }
#endif
        const std::scoped_lock guard(ids_mutex_);
        if (color_mode_)
        {
            // Remove any existing color callback before (re-)adding to update
            // the verbosity threshold or handle toggling.
            if (color_callback_active_)
            {
                loguru::remove_callback(k_color_cb_id);
                color_callback_active_ = false;
            }
            if (enabled)
            {
                loguru::add_callback(k_color_cb_id,
                    color_stderr_callback,
                    nullptr,
                    static_cast<loguru::Verbosity>(requested_verbosity_));
                color_callback_active_ = true;
            }
            // Suppress loguru's own (uncolored) stderr so there is no duplicate.
            loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
        }
        else
        {
            if (color_callback_active_)
            {
                loguru::remove_callback(k_color_cb_id);
                color_callback_active_ = false;
            }
            loguru::g_stderr_verbosity =
                enabled ? static_cast<loguru::Verbosity>(requested_verbosity_)
                        : loguru::Verbosity_OFF;
        }
    }

    static constexpr const char* k_color_cb_id = "logging_color_stderr";

    // Per-thread name shadow (loguru tracks its own too, but we mirror it so
    // get_thread_name is consistent across backends).
    static inline thread_local char g_thread_name[128] = {};

    std::atomic<bool>     console_mode_{true};
    logger_verbosity_enum requested_verbosity_{logger_verbosity_enum::VERBOSITY_INFO};
    logger_verbosity_enum internal_verbosity_{logger_verbosity_enum::VERBOSITY_INFO};
    bool                  color_mode_{true};
    bool                  color_callback_active_{false};

    std::mutex                      ids_mutex_;
    std::unordered_set<std::string> registered_ids_;
};

}  // namespace detail
}  // namespace backend
}  // namespace logging

#endif  // LOGGING_HAS_LOGURU
#endif  // LOGGING_INCLUDE_BACKEND_LOGURU_BACKEND_H
