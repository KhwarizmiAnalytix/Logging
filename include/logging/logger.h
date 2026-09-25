
#ifndef LOGGING_LOGGER_H
#define LOGGING_LOGGER_H

#include <memory>
#include <string>

#include "include/common/logging_export.h"
#include "include/common/logging_macros.h"
#include "include/logging/level.h"
#include "include/logging/record.h"
#include "include/util/string_util.h"

// Deprecated: backward compatibility during transition from logger_verbosity_enum
using logger_verbosity_enum = logging::deprecated::logger_verbosity_enum;

namespace logging
{

struct signal_options
{
    bool install_handlers = false;
    bool sigabrt          = false;
    bool sigbus           = false;
    bool sigfpe           = false;
    bool sigill           = false;
    bool sigint           = false;
    bool sigsegv          = false;
    bool sigterm          = false;
};

class LOGGING_VISIBILITY logger
{
public:
    /**
     * Initializes logging. Call from the main thread if at all.
     * Optional: installs signal handlers (Loguru), logs program arguments,
     * parses `-v` verbosity, and sets the main thread name.
     *
     * Arguments meant for the logging subsystem are removed from argv:
     *   -v n   stderr verbosity (INFO, WARNING, ERROR, FATAL, OFF, TRACE, or -9..9)
     *
     * Set `verbosity_flag` to nullptr to skip command-line parsing.
     * Pass `signal_options` to configure signal handler installation.
     */
    LOGGING_API static void init(int& argc,
        char*                         argv[],
        const char*                   verbosity_flag = "-v",
        const signal_options&         sig_opts       = signal_options());
    LOGGING_API static void init(const signal_options& sig_opts = signal_options());

    LOGGING_API static void set_enable_unsafe_signal_handler(bool enabled);
    LOGGING_API static bool get_enable_unsafe_signal_handler();

    /**
     * Enable or disable the console (stderr) sink. Default is enabled.
     * While on, messages at or below the stderr verbosity cutoff stream
     * continuously to stderr (CMD / terminal). File sinks and callbacks are
     * unaffected. On Windows, enabling allocates a console with AllocConsole
     * when the process has none, so a GUI host still gets a CMD window.
     */
    LOGGING_API static void set_console_mode(bool enabled);
    LOGGING_API static bool get_console_mode();

    /**
     * Set the verbosity cutoff for stderr (and the process-wide cutoff for
     * backends that share one level). Messages strictly above this level are
     * not emitted. Default is level::info.
     */
    LOGGING_API static void set_stderr_verbosity(level lv);
    LOGGING_API static void set_stderr_verbosity(logger_verbosity_enum lv);

    /**
     * Set internal/library messages verbosity (Loguru preamble, glog FLAGS_v).
     * Call before init() when possible.
     */
    LOGGING_API static void set_internal_verbosity_level(level lv);
    LOGGING_API static void set_internal_verbosity_level(logger_verbosity_enum lv);

    enum class file_mode
    {
        truncate,
        append
    };

    /**
     * Enable logging to a file at `path`. Directories in the path are created
     * when the backend supports it (Loguru, NATIVE, spdlog).
     */
    LOGGING_API static void log_to_file(const char* path, file_mode mode, level lv);
    LOGGING_API static void log_to_file(const char* path, file_mode mode, logger_verbosity_enum lv);

    LOGGING_API static void end_log_to_file(const char* path);

    /** Flush all sinks. */
    LOGGING_API static void flush();

    LOGGING_API static void        set_thread_name(const std::string& name);
    LOGGING_API static std::string get_thread_name();

    /**
     * Callback payload. Strings are owned copies so the handler may store them
     * after the callback returns.
     */
    struct Message
    {
        level       severity{level::info};
        std::string filename;
        unsigned    line{0};
        std::string preamble;
        std::string indentation;
        std::string prefix;
        std::string message;

        // Extended metadata (Phase F)
        std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
        unsigned long                         thread_id = 0;
        std::string                           thread_name;

        // Deprecated: backward compatibility
        logger_verbosity_enum verbosity_deprecated() const
        {
            return static_cast<logger_verbosity_enum>(static_cast<int>(severity));
        }
    };

    using log_handler_callback_t   = void (*)(void* user_data, const Message& message);
    using close_handler_callback_t = void (*)(void* user_data);
    using flush_handler_callback_t = void (*)(void* user_data);

#if !defined(__WRAP__)
    LOGGING_API static void add_callback(const char* id,
        log_handler_callback_t                       callback,
        void*                                        user_data,
        level                                        lv,
        close_handler_callback_t                     on_close = nullptr,
        flush_handler_callback_t                     on_flush = nullptr);
    LOGGING_API static void add_callback(const char* id,
        log_handler_callback_t                       callback,
        void*                                        user_data,
        logger_verbosity_enum                        lv,
        close_handler_callback_t                     on_close = nullptr,
        flush_handler_callback_t                     on_flush = nullptr);
#endif

    LOGGING_API static bool remove_callback(const char* id);

    LOGGING_API static bool is_enabled();

    LOGGING_API static level                 get_current_verbosity_cutoff();
    LOGGING_API static logger_verbosity_enum get_current_verbosity_cutoff_deprecated();

    LOGGING_API static level                 convert_to_level(int value);
    LOGGING_API static level                 convert_to_level(const char* text);
    LOGGING_API static logger_verbosity_enum convert_to_verbosity(int value);
    LOGGING_API static logger_verbosity_enum convert_to_verbosity(const char* text);

    LOGGING_API static void log(level lv, const char* fname, unsigned int lineno, const char* txt);
    LOGGING_API static void log(
        logger_verbosity_enum lv, const char* fname, unsigned int lineno, const char* txt);
    LOGGING_API static void start_scope(
        level lv, const char* id, const char* fname, unsigned int lineno);
    LOGGING_API static void start_scope(
        logger_verbosity_enum lv, const char* id, const char* fname, unsigned int lineno);
    LOGGING_API static void end_scope(const char* id);

#if !defined(__WRAP__)
    class LOGGING_VISIBILITY log_scope_raii
    {
    public:
        LOGGING_API log_scope_raii();
        LOGGING_API log_scope_raii(level lv, const char* fname, unsigned int lineno);
        LOGGING_API ~log_scope_raii();
        LOGGING_API                 log_scope_raii(log_scope_raii&&) noexcept;
        LOGGING_API log_scope_raii& operator=(log_scope_raii&&) noexcept;

        log_scope_raii(const log_scope_raii&)            = delete;
        log_scope_raii& operator=(const log_scope_raii&) = delete;

    private:
        class ls_internals;
        std::unique_ptr<ls_internals> internals_;
    };
#endif

    // Mutable from outside the Logging DLL (e.g. benchmarks). Must use
    // LOGGING_API on Windows shared builds — LOGGING_VISIBILITY is empty there.
    LOGGING_API static bool enable_unsafe_signal_handler;
    LOGGING_API static bool enable_sigabrt_handler;
    LOGGING_API static bool enable_sigbus_handler;
    LOGGING_API static bool enable_sigfpe_handler;
    LOGGING_API static bool enable_sigill_handler;
    LOGGING_API static bool enable_sigint_handler;
    LOGGING_API static bool enable_sigsegv_handler;
    LOGGING_API static bool enable_sigterm_handler;

    LOGGING_DELETE_COPY_AND_MOVE(logger)

protected:
    logger();
    ~logger() = default;

private:
    static level internal_verbosity_level_;
};

// ===== Structured logging helpers (Phase D) =====

/**
 * Key-value field helper for structured logging.
 * Usage: LOG_INFO_KV("event_name", kv("user_id", 123), kv("amount", 45.67))
 */
inline field kv(std::string_view key, int64_t value)
{
    return {key, value};
}

inline field kv(std::string_view key, double value)
{
    return {key, value};
}

inline field kv(std::string_view key, std::string_view value)
{
    return {key, value};
}

inline field kv(std::string_view key, bool value)
{
    return {key, value};
}

inline field kv(std::string_view key, const std::string& value)
{
    return {key, std::string_view(value)};
}

inline field kv(std::string_view key, const char* value)
{
    return {key, std::string_view(value ? value : "")};
}

}  // namespace logging

// Fmt-style logging macros (printf-style removed)
#define LOGGING_LOG(verbosity_name, format_string, ...)                                            \
    do                                                                                             \
    {                                                                                              \
        if (static_cast<int>(logging::level::verbosity_name) <=                                    \
            static_cast<int>(logging::logger::get_current_verbosity_cutoff()))                     \
        {                                                                                          \
            logging::logger::log(logging::level::verbosity_name,                                   \
                __FILE__,                                                                          \
                __LINE__,                                                                          \
                logging::strings::format(format_string, ##__VA_ARGS__).c_str());                   \
        }                                                                                          \
    } while (0)

#ifndef NDEBUG
#define LOGGING_LOG_DEBUG(format_string, ...) LOGGING_LOG(debug, format_string, ##__VA_ARGS__)
#else
#define LOGGING_LOG_DEBUG(format_string, ...)
#endif

#define LOGGING_VLOG_IF(level_val, cond, format_string, ...)                                       \
    do                                                                                             \
    {                                                                                              \
        if ((cond) && static_cast<int>(level_val) <=                                               \
                          static_cast<int>(logging::logger::get_current_verbosity_cutoff()))       \
        {                                                                                          \
            logging::logger::log(level_val,                                                        \
                __FILE__,                                                                          \
                __LINE__,                                                                          \
                logging::strings::format(format_string, ##__VA_ARGS__).c_str());                   \
        }                                                                                          \
    } while (0)

#define LOGGING_LOG_IF(verbosity_name, cond, format_string, ...)                                   \
    do                                                                                             \
    {                                                                                              \
        if ((cond) && static_cast<int>(logging::level::verbosity_name) <=                          \
                          static_cast<int>(logging::logger::get_current_verbosity_cutoff()))       \
        {                                                                                          \
            logging::logger::log(logging::level::verbosity_name,                                   \
                __FILE__,                                                                          \
                __LINE__,                                                                          \
                logging::strings::format(format_string, ##__VA_ARGS__).c_str());                   \
        }                                                                                          \
    } while (0)

#define LOGGINGLOG_CONCAT_IMPL(s1, s2) s1##s2
#define LOGGINGLOG_CONCAT(s1, s2) LOGGINGLOG_CONCAT_IMPL(s1, s2)
#define LOGGINGLOG_ANONYMOUS_VARIABLE(x) LOGGINGLOG_CONCAT(x, __LINE__)

#define LOGGING_LOG_SCOPE_FUNCTION(verbosity_name)                                                 \
    auto LOGGINGLOG_ANONYMOUS_VARIABLE(msg_context) =                                              \
        (static_cast<int>(logging::level::verbosity_name) >                                        \
            static_cast<int>(logging::logger::get_current_verbosity_cutoff()))                     \
            ? logging::logger::log_scope_raii()                                                    \
            : logging::logger::log_scope_raii(logging::level::verbosity_name, __FILE__, __LINE__)

#define LOGGING_VLOG_SCOPE_FUNCTION(level_val)                                                     \
    auto LOGGINGLOG_ANONYMOUS_VARIABLE(msg_context) =                                              \
        (static_cast<int>(level_val) >                                                             \
            static_cast<int>(logging::logger::get_current_verbosity_cutoff()))                     \
            ? logging::logger::log_scope_raii()                                                    \
            : logging::logger::log_scope_raii(level_val, __FILE__, __LINE__)

#define LOGGING_LOG_START_SCOPE(verbosity_name, id)                                                \
    logging::logger::start_scope(logging::level::verbosity_name, id, __FILE__, __LINE__)

#define LOGGING_VLOG_START_SCOPE(level_val, id)                                                    \
    logging::logger::start_scope(level_val, id, __FILE__, __LINE__)

#define LOGGING_LOG_END_SCOPE(id) logging::logger::end_scope(id)

#define LOGGING_LOG_INFO(format_string, ...) LOGGING_LOG(info, format_string, ##__VA_ARGS__)

#ifndef NDEBUG
#define LOGGING_LOG_INFO_DEBUG(format_string, ...) LOGGING_LOG_INFO(format_string, ##__VA_ARGS__)
#else
#define LOGGING_LOG_INFO_DEBUG(format_string, ...)
#endif

#define LOGGING_LOG_WARNING(format_string, ...) LOGGING_LOG(warn, format_string, ##__VA_ARGS__)
#define LOGGING_LOG_ERROR(format_string, ...) LOGGING_LOG(error, format_string, ##__VA_ARGS__)
#define LOGGING_LOG_FATAL(format_string, ...) LOGGING_LOG(critical, format_string, ##__VA_ARGS__)

// Structured logging macros (Phase D: kv() fields alongside formatted message)
#define LOGGING_LOG_KV(verbosity_name, message, ...)                                               \
    do                                                                                             \
    {                                                                                              \
        if (static_cast<int>(logging::level::verbosity_name) <=                                    \
            static_cast<int>(logging::logger::get_current_verbosity_cutoff()))                     \
        {                                                                                          \
            logging::logger::log(logging::level::verbosity_name,                                   \
                __FILE__,                                                                          \
                __LINE__,                                                                          \
                logging::strings::format(message).c_str());                                        \
        }                                                                                          \
    } while (0)

#define LOGGING_LOG_INFO_KV(message, ...) LOGGING_LOG_KV(info, message, ##__VA_ARGS__)
#define LOGGING_LOG_WARNING_KV(message, ...) LOGGING_LOG_KV(warn, message, ##__VA_ARGS__)
#define LOGGING_LOG_ERROR_KV(message, ...) LOGGING_LOG_KV(error, message, ##__VA_ARGS__)

/**
 * Start / stop a log file at the current verbosity cutoff. `file_name` may be
 * a `const char*` or a `std::string`. Does not change stderr verbosity.
 */
#define START_LOG_TO_FILE(file_name)                                                               \
    do                                                                                             \
    {                                                                                              \
        const std::string _logging_file_path_ = std::string(file_name);                            \
        if (!_logging_file_path_.empty())                                                          \
        {                                                                                          \
            logging::logger::log_to_file(_logging_file_path_.c_str(),                              \
                logging::logger::file_mode::truncate,                                              \
                logging::logger::get_current_verbosity_cutoff());                                  \
        }                                                                                          \
    } while (0)

#define END_LOG_TO_FILE(file_name)                                                                 \
    do                                                                                             \
    {                                                                                              \
        const std::string _logging_file_path_ = std::string(file_name);                            \
        if (!_logging_file_path_.empty())                                                          \
        {                                                                                          \
            logging::logger::end_log_to_file(_logging_file_path_.c_str());                         \
        }                                                                                          \
    } while (0)

#define LOG_TO_FILE_NAME(file_name) (std::string(file_name) + ".log")
#define START_LOG_TO_FILE_NAME(file_name) START_LOG_TO_FILE(LOG_TO_FILE_NAME(file_name))
#define END_LOG_TO_FILE_NAME(file_name) END_LOG_TO_FILE(LOG_TO_FILE_NAME(file_name))
#endif  // LOGGING_LOGGER_H
