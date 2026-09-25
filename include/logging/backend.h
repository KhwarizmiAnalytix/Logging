
#ifndef LOGGING_BACKEND_H
#define LOGGING_BACKEND_H

#include <memory>
#include <string>

#include "include/logger/logger.h"
#include "include/logger/logger_verbosity_enum.h"

namespace logging
{
namespace backend
{

/**
 * Per-scope state returned by Backend::scope_enter. The scope's exit action
 * (emit "[scope exit]", pop a Loguru LogScopeRAII, etc.) runs in the derived
 * type's destructor, so callers close a scope simply by destroying the token.
 */
struct scope_state
{
    virtual ~scope_state() = default;
};

/**
 * One-time initialization inputs handed to the selected backend. The facade
 * owns the signal-handler policy and thread name; the backend performs only
 * its own library setup (loguru::init, google::InitGoogleLogging, ...).
 */
struct init_options
{
    const char* verbosity_flag        = "-v";
    const char* main_thread_name      = nullptr;  // null when unset
    bool        unsafe_signal_handler = false;
    bool        sigabrt               = false;
    bool        sigbus                = false;
    bool        sigfpe                = false;
    bool        sigill                = false;
    bool        sigint                = false;
    bool        sigsegv               = false;
    bool        sigterm               = false;
};

/**
 * Abstract sink for the core I/O path. Exactly one concrete backend is built
 * at process start and reached through active_backend(); the selection is the
 * single remaining compile-time branch (see factory.cpp). Levels use the
 * library's logger_verbosity_enum so the interface stays type-safe.
 */
class Backend
{
public:
    virtual ~Backend() = default;

    virtual void log(
        logger_verbosity_enum severity, const char* fname, unsigned line, const char* msg) = 0;

    virtual logger_verbosity_enum get_cutoff() const                                     = 0;
    virtual void                  set_stderr_verbosity(logger_verbosity_enum severity)   = 0;
    virtual void                  set_internal_verbosity(logger_verbosity_enum severity) = 0;

    virtual void set_console_mode(bool enabled) = 0;
    virtual bool get_console_mode() const       = 0;

    virtual void log_to_file(
        const char* path, logger::file_mode mode, logger_verbosity_enum severity) = 0;
    virtual void end_log_to_file(const char* path)                                = 0;

    virtual void flush() = 0;

    virtual void        set_thread_name(const std::string& name) = 0;
    virtual std::string get_thread_name() const                  = 0;

    virtual void add_callback(const char* id,
        logger::log_handler_callback_t    callback,
        void*                             user_data,
        logger_verbosity_enum             severity,
        logger::close_handler_callback_t  on_close,
        logger::flush_handler_callback_t  on_flush) = 0;
    virtual bool remove_callback(const char* id)   = 0;

    virtual std::unique_ptr<scope_state> scope_enter(logger_verbosity_enum severity,
        const char*                                                        fname,
        unsigned                                                           line,
        const std::string&                                                 msg) = 0;

    // Backend-specific one-time setup (library init, argv verbosity flag, etc.).
    virtual void on_init(int& argc, char* argv[], const init_options& options) = 0;
};

// Defined once per backend, but only the selected backend's factory is called
// (and thus linked) — the others are declared for the single #if in factory.cpp.
std::unique_ptr<Backend> create_native_backend();
std::unique_ptr<Backend> create_spdlog_backend();
std::unique_ptr<Backend> create_loguru_backend();
std::unique_ptr<Backend> create_glog_backend();

// The process-wide singleton, constructed on first use from the selected backend.
Backend& active_backend();

}  // namespace backend
}  // namespace logging
#endif  // LOGGING_BACKEND_H
