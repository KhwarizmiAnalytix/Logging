#include "include/logging/backend.h"

#if LOGGING_HAS_GLOG

#include <atomic>
#include <cstring>
#include <memory>
#include <string>

#include <glog/logging.h>

#include "src/backend/backend_shared.h"

namespace logging
{
namespace backend
{
namespace
{
using shared::copy_thread_name;
using shared::ensure_parent_directory;

thread_local char g_thread_name[128] = {};

class GlogBackend : public Backend
{
public:
    void log(
        logger_verbosity_enum severity, const char* fname, unsigned line, const char* msg) override
    {
        const char* text = (msg != nullptr) ? msg : "";
        if (!google::IsGoogleLoggingInitialized())
        {
            google::InitGoogleLogging((fname != nullptr) ? fname : "logging");
            FLAGS_colorlogtostderr = true;
            apply_console();
        }
        const char* file = (fname != nullptr) ? fname : "unknown";
        const int   ln   = static_cast<int>(line);
        if (severity == logger_verbosity_enum::VERBOSITY_ERROR)
        {
            google::LogMessage(file, ln, google::GLOG_ERROR).stream() << text;
        }
        else if (severity == logger_verbosity_enum::VERBOSITY_WARNING)
        {
            google::LogMessage(file, ln, google::GLOG_WARNING).stream() << text;
        }
        else if (severity == logger_verbosity_enum::VERBOSITY_INFO)
        {
            google::LogMessage(file, ln, google::GLOG_INFO).stream() << text;
        }
        else if (severity > logger_verbosity_enum::VERBOSITY_INFO)
        {
            VLOG(static_cast<int>(severity)) << text;
        }
        else
        {
            google::LogMessageFatal(file, ln).stream() << text;
        }
    }

    logger_verbosity_enum get_cutoff() const override
    {
        if (FLAGS_v > 0)
        {
            const int v = FLAGS_v;
            return static_cast<logger_verbosity_enum>(
                v > static_cast<int>(logger_verbosity_enum::VERBOSITY_MAX)
                    ? static_cast<int>(logger_verbosity_enum::VERBOSITY_MAX)
                    : v);
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
    }

    void set_stderr_verbosity(logger_verbosity_enum severity) override
    {
        requested_verbosity_ = severity;
        apply_verbosity_flags(severity);
        apply_console();
    }

    void set_internal_verbosity(logger_verbosity_enum severity) override
    {
        FLAGS_v = static_cast<int>(severity);
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
        has_file_ = true;
        apply_console();
        google::SetLogDestination(google::GLOG_INFO, path);
        google::SetLogDestination(google::GLOG_WARNING, path);
        google::SetLogDestination(google::GLOG_ERROR, path);
        google::SetLogDestination(google::GLOG_FATAL, path);
        (void)mode;
        (void)severity;
    }

    void end_log_to_file(const char* path) override
    {
        google::FlushLogFiles(google::GLOG_INFO);
        has_file_ = false;
        apply_console();
        (void)path;
    }

    void flush() override { google::FlushLogFiles(google::GLOG_INFO); }

    void set_thread_name(const std::string& name) override
    {
        copy_thread_name(g_thread_name, sizeof(g_thread_name), name);
    }

    std::string get_thread_name() const override
    {
        if (std::strlen(g_thread_name) > 0)
        {
            return {g_thread_name};
        }
        return {"N/A"};
    }

    // glog has no custom-callback mechanism (see backend_traits.h).
    void add_callback(const char*,
        logger::log_handler_callback_t,
        void*,
        logger_verbosity_enum,
        logger::close_handler_callback_t,
        logger::flush_handler_callback_t) override
    {
    }

    bool remove_callback(const char*) override { return false; }

    std::unique_ptr<scope_state> scope_enter(logger_verbosity_enum severity,
        const char*                                                fname,
        unsigned                                                   line,
        const std::string&                                         msg) override
    {
        log(severity, fname, line, ("[scope enter] " + msg).c_str());
        return std::make_unique<GlogScope>(*this, severity, fname, line, msg);
    }

    void on_init(int& argc, char* argv[], const init_options& options) override
    {
        if (!google::IsGoogleLoggingInitialized())
        {
            google::InitGoogleLogging(argc > 0 && argv != nullptr ? argv[0] : "logging");
        }
        FLAGS_colorlogtostderr = true;
        if (options.unsafe_signal_handler)
        {
            google::InstallFailureSignalHandler();
        }
        apply_console();
    }

private:
    class GlogScope : public scope_state
    {
    public:
        GlogScope(GlogBackend&    owner,
            logger_verbosity_enum severity,
            const char*           fname,
            unsigned              line,
            std::string           msg)
            : owner_(owner), severity_(severity), fname_(fname != nullptr ? fname : ""),
              line_(line), msg_(std::move(msg))
        {
        }

        ~GlogScope() override
        {
            owner_.log(severity_, fname_.c_str(), line_, ("[scope exit] " + msg_).c_str());
        }

    private:
        GlogBackend&          owner_;
        logger_verbosity_enum severity_;
        std::string           fname_;
        unsigned              line_;
        std::string           msg_;
    };

    void apply_verbosity_flags(logger_verbosity_enum level)
    {
        if (level <= logger_verbosity_enum::VERBOSITY_OFF)
        {
            FLAGS_minloglevel     = google::GLOG_FATAL + 1;
            FLAGS_stderrthreshold = google::GLOG_FATAL + 1;
            FLAGS_v               = 0;
        }
        else if (level <= logger_verbosity_enum::VERBOSITY_FATAL)
        {
            FLAGS_minloglevel     = google::GLOG_FATAL;
            FLAGS_stderrthreshold = google::GLOG_FATAL;
            FLAGS_v               = 0;
        }
        else if (level <= logger_verbosity_enum::VERBOSITY_ERROR)
        {
            FLAGS_minloglevel     = google::GLOG_ERROR;
            FLAGS_stderrthreshold = google::GLOG_ERROR;
            FLAGS_v               = 0;
        }
        else if (level <= logger_verbosity_enum::VERBOSITY_WARNING)
        {
            FLAGS_minloglevel     = google::GLOG_WARNING;
            FLAGS_stderrthreshold = google::GLOG_WARNING;
            FLAGS_v               = 0;
        }
        else if (level <= logger_verbosity_enum::VERBOSITY_INFO)
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

    void apply_console()
    {
        const bool enabled = console_mode_.load(std::memory_order_relaxed);
        if (has_file_)
        {
            FLAGS_logtostderr     = false;
            FLAGS_alsologtostderr = enabled;
        }
        else
        {
            FLAGS_logtostderr     = enabled;
            FLAGS_alsologtostderr = false;
        }
        if (!enabled)
        {
            FLAGS_stderrthreshold = google::GLOG_FATAL + 1;
        }
        else
        {
            apply_verbosity_flags(requested_verbosity_);
        }
    }

    std::atomic<bool>     console_mode_{true};
    bool                  has_file_{false};
    logger_verbosity_enum requested_verbosity_{logger_verbosity_enum::VERBOSITY_INFO};
};

}  // namespace

std::unique_ptr<Backend> create_glog_backend()
{
    return std::make_unique<GlogBackend>();
}

}  // namespace backend
}  // namespace logging

#endif  // LOGGING_HAS_GLOG
