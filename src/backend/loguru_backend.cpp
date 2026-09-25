#include "include/logging/backend.h"

#if LOGGING_HAS_LOGURU

#include <atomic>
#include <cstring>
#include <memory>
#include <string>

#include <loguru.hpp>

#include "src/backend/backend_shared.h"

namespace logging
{
namespace backend
{
namespace
{
using shared::abort_if_fatal;
using shared::copy_thread_name;
using shared::ensure_parent_directory;

// Bridge from loguru's callback ABI to the library's typed callbacks. Heap
// allocated per registration and freed by the loguru close bridge.
struct CallbackBridgeData
{
    logger::log_handler_callback_t   handler{nullptr};
    logger::close_handler_callback_t close{nullptr};
    logger::flush_handler_callback_t flush{nullptr};
    void*                            inner_data{nullptr};
};

void callback_bridge_handler(void* user_data, const loguru::Message& message)
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

void callback_bridge_close(void* user_data)
{
    auto* data = static_cast<CallbackBridgeData*>(user_data);
    if (data->close != nullptr)
    {
        data->close(data->inner_data);
        data->inner_data = nullptr;
    }
    delete data;
}

void callback_bridge_flush(void* user_data)
{
    auto* data = static_cast<CallbackBridgeData*>(user_data);
    if (data->flush != nullptr)
    {
        data->flush(data->inner_data);
    }
}

// Per-thread name shadow (loguru tracks its own too, but we mirror it so
// get_thread_name is consistent across backends).
thread_local char g_thread_name[128] = {};

class LoguruBackend : public Backend
{
public:
    void log(
        logger_verbosity_enum severity, const char* fname, unsigned line, const char* msg) override
    {
        const char* text = (msg != nullptr) ? msg : "";
        loguru::log(static_cast<loguru::Verbosity>(severity), fname, line, "%s", text);
        abort_if_fatal(severity);
    }

    logger_verbosity_enum get_cutoff() const override
    {
        return static_cast<logger_verbosity_enum>(loguru::current_verbosity_cutoff());
    }

    void set_stderr_verbosity(logger_verbosity_enum severity) override
    {
        requested_verbosity_ = severity;
        apply_console();
    }

    void set_internal_verbosity(logger_verbosity_enum severity) override
    {
        internal_verbosity_          = severity;
        loguru::g_internal_verbosity = static_cast<loguru::Verbosity>(severity);
    }

    void set_console_mode(bool enabled) override
    {
        console_mode_.store(enabled, std::memory_order_relaxed);
        apply_console();
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
        const loguru::FileMode loguru_mode =
            (mode == logger::file_mode::append) ? loguru::Append : loguru::Truncate;
        loguru::add_file(path, loguru_mode, static_cast<loguru::Verbosity>(severity));
    }

    void end_log_to_file(const char* path) override
    {
        if (path != nullptr)
        {
            loguru::remove_callback(path);
        }
    }

    void flush() override { loguru::flush(); }

    void set_thread_name(const std::string& name) override
    {
        loguru::set_thread_name(name.c_str());
        copy_thread_name(g_thread_name, sizeof(g_thread_name), name);
    }

    std::string get_thread_name() const override
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
        logger::flush_handler_callback_t on_flush) override
    {
        auto* callback_data = new CallbackBridgeData{callback, on_close, on_flush, user_data};
        loguru::add_callback(id,
            callback_bridge_handler,
            callback_data,
            static_cast<loguru::Verbosity>(severity),
            callback_bridge_close,
            callback_bridge_flush);
    }

    bool remove_callback(const char* id) override { return loguru::remove_callback(id); }

    std::unique_ptr<scope_state> scope_enter(logger_verbosity_enum severity,
        const char*                                                fname,
        unsigned                                                   line,
        const std::string&                                         msg) override
    {
        return std::make_unique<LoguruScope>(severity, fname, line, msg);
    }

    void on_init(int& argc, char* argv[], const init_options& options) override
    {
        loguru::g_preamble_date      = false;
        loguru::g_preamble_time      = false;
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
        loguru::init(argc, argv, loguru_options);
        loguru::g_stderr_verbosity = current_stderr_verbosity;
        apply_console();
    }

private:
    class LoguruScope : public scope_state
    {
    public:
        LoguruScope(logger_verbosity_enum severity,
            const char*                   fname,
            unsigned                      line,
            const std::string&            msg)
            : data_(std::make_unique<loguru::LogScopeRAII>(
                  static_cast<loguru::Verbosity>(severity), fname, line, "%s", msg.c_str()))
        {
        }

    private:
        std::unique_ptr<loguru::LogScopeRAII> data_;
    };

    void apply_console()
    {
        const bool enabled = console_mode_.load(std::memory_order_relaxed);
#if defined(_WIN32)
        if (enabled)
        {
            shared::ensure_windows_console();
        }
#endif
        loguru::g_stderr_verbosity =
            enabled ? static_cast<loguru::Verbosity>(requested_verbosity_) : loguru::Verbosity_OFF;
    }

    std::atomic<bool>     console_mode_{true};
    logger_verbosity_enum requested_verbosity_{logger_verbosity_enum::VERBOSITY_INFO};
    logger_verbosity_enum internal_verbosity_{logger_verbosity_enum::VERBOSITY_INFO};
};

}  // namespace

std::unique_ptr<Backend> create_loguru_backend()
{
    return std::make_unique<LoguruBackend>();
}

}  // namespace backend
}  // namespace logging

#endif  // LOGGING_HAS_LOGURU
