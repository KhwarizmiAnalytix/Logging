#pragma once

#include <memory>
#include <string>

#include "common/logging_export.h"
#include "common/logging_macros.h"
#include "logger_verbosity_enum.h"
#include "util/string_util.h"

#if defined(__clang__) || defined(__GNUC__)
#define LOGGING_PRINTF_LIKE(fmtarg, firstvararg) \
    __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#define LOGGING_FORMAT_STRING_TYPE const char*
#elif defined(_MSC_VER)
#define LOGGING_PRINTF_LIKE(fmtarg, firstvararg)
#define LOGGING_FORMAT_STRING_TYPE _In_z_ _Printf_format_string_ const char*
#else
#define LOGGING_PRINTF_LIKE(fmtarg, firstvararg)
#define LOGGING_FORMAT_STRING_TYPE const char*
#endif

namespace logging
{
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
     * Set `enable_unsafe_signal_handler` to false before init() to skip
     * signal-handler installation (Loguru and glog). Prefer
     * set_enable_unsafe_signal_handler() when assigning from another shared
     * library / executable on Windows.
     */
    LOGGING_API static void init(int& argc, char* argv[], const char* verbosity_flag = "-v");
    LOGGING_API static void init();

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
     * not emitted. Default is VERBOSITY_INFO (0).
     */
    LOGGING_API static void set_stderr_verbosity(logger_verbosity_enum level);

    /**
     * Set internal/library messages verbosity (Loguru preamble, glog FLAGS_v).
     * Call before init() when possible.
     */
    LOGGING_API static void set_internal_verbosity_level(logger_verbosity_enum level);

    enum class file_mode
    {
        truncate,
        append
    };

    /**
     * Enable logging to a file at `path`. Directories in the path are created
     * when the backend supports it (Loguru, NATIVE, spdlog).
     */
    LOGGING_API static void log_to_file(
        const char* path, file_mode mode, logger_verbosity_enum verbosity);

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
        logger_verbosity_enum verbosity{logger_verbosity_enum::VERBOSITY_INFO};
        std::string           filename;
        unsigned              line{0};
        std::string           preamble;
        std::string           indentation;
        std::string           prefix;
        std::string           message;
    };

    using log_handler_callback_t   = void (*)(void* user_data, const Message& message);
    using close_handler_callback_t = void (*)(void* user_data);
    using flush_handler_callback_t = void (*)(void* user_data);

#if !defined(__WRAP__)
    LOGGING_API static void add_callback(
        const char*              id,
        log_handler_callback_t   callback,
        void*                    user_data,
        logger_verbosity_enum    verbosity,
        close_handler_callback_t on_close = nullptr,
        flush_handler_callback_t on_flush = nullptr);
#endif

    LOGGING_API static bool remove_callback(const char* id);

    LOGGING_API static bool is_enabled();

    LOGGING_API static logger_verbosity_enum get_current_verbosity_cutoff();

    LOGGING_API static logger_verbosity_enum convert_to_verbosity(int value);
    LOGGING_API static logger_verbosity_enum convert_to_verbosity(const char* text);

    LOGGING_API static void log(
        logger_verbosity_enum verbosity, const char* fname, unsigned int lineno, const char* txt);
    LOGGING_API static void start_scope(
        logger_verbosity_enum verbosity, const char* id, const char* fname, unsigned int lineno);
    LOGGING_API static void end_scope(const char* id);

#if !defined(__WRAP__)
    LOGGING_API static void log_f(
        logger_verbosity_enum      verbosity,
        const char*                fname,
        unsigned int               lineno,
        LOGGING_FORMAT_STRING_TYPE format,
        ...) LOGGING_PRINTF_LIKE(4, 5);
    LOGGING_API static void start_scope_f(
        logger_verbosity_enum      verbosity,
        const char*                id,
        const char*                fname,
        unsigned int               lineno,
        LOGGING_FORMAT_STRING_TYPE format,
        ...) LOGGING_PRINTF_LIKE(5, 6);

    class LOGGING_VISIBILITY log_scope_raii
    {
    public:
        LOGGING_API log_scope_raii();
        LOGGING_API log_scope_raii(
            logger_verbosity_enum      verbosity,
            const char*                fname,
            unsigned int               lineno,
            LOGGING_FORMAT_STRING_TYPE format,
            ...) LOGGING_PRINTF_LIKE(5, 6);
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
    ~logger();

private:
    static logger_verbosity_enum internal_verbosity_level_;
};
}  // namespace logging

#define LOGGING_LOG(verbosity_name, format_string, ...)                          \
    do                                                                           \
    {                                                                            \
        if (logging::logger_verbosity_enum::VERBOSITY_##verbosity_name <=        \
            logging::logger::get_current_verbosity_cutoff())                     \
        {                                                                        \
            logging::logger::log(                                                \
                logging::logger_verbosity_enum::VERBOSITY_##verbosity_name,      \
                __FILE__,                                                        \
                __LINE__,                                                        \
                logging::strings::format(format_string, ##__VA_ARGS__).c_str()); \
        }                                                                        \
    } while (0)

#ifndef NDEBUG
#define LOGGING_LOG_DEBUG(verbosity_name, format_string, ...) \
    LOGGING_LOG(verbosity_name, format_string, ##__VA_ARGS__)
#else
#define LOGGING_LOG_DEBUG(verbosity_name, format_string, ...)
#endif

#define LOGGING_VLOG_IF(level, cond, format_string, ...)                         \
    do                                                                           \
    {                                                                            \
        if ((cond) && static_cast<logging::logger_verbosity_enum>(level) <=      \
                          logging::logger::get_current_verbosity_cutoff())       \
        {                                                                        \
            logging::logger::log(                                                \
                static_cast<logging::logger_verbosity_enum>(level),              \
                __FILE__,                                                        \
                __LINE__,                                                        \
                logging::strings::format(format_string, ##__VA_ARGS__).c_str()); \
        }                                                                        \
    } while (0)

#define LOGGING_LOG_IF(verbosity_name, cond, format_string, ...)                    \
    do                                                                              \
    {                                                                               \
        if ((cond) && logging::logger_verbosity_enum::VERBOSITY_##verbosity_name <= \
                          logging::logger::get_current_verbosity_cutoff())          \
        {                                                                           \
            logging::logger::log(                                                   \
                logging::logger_verbosity_enum::VERBOSITY_##verbosity_name,         \
                __FILE__,                                                           \
                __LINE__,                                                           \
                logging::strings::format(format_string, ##__VA_ARGS__).c_str());    \
        }                                                                           \
    } while (0)

#define LOGGINGLOG_CONCAT_IMPL(s1, s2) s1##s2
#define LOGGINGLOG_CONCAT(s1, s2) LOGGINGLOG_CONCAT_IMPL(s1, s2)
#define LOGGINGLOG_ANONYMOUS_VARIABLE(x) LOGGINGLOG_CONCAT(x, __LINE__)

#define LOGGING_LOG_SCOPE_FUNCTION(verbosity_name)                            \
    auto LOGGINGLOG_ANONYMOUS_VARIABLE(msg_context) =                         \
        (logging::logger_verbosity_enum::VERBOSITY_##verbosity_name >         \
         logging::logger::get_current_verbosity_cutoff())                     \
            ? logging::logger::log_scope_raii()                               \
            : logging::logger::log_scope_raii(                                \
                  logging::logger_verbosity_enum::VERBOSITY_##verbosity_name, \
                  __FILE__,                                                   \
                  __LINE__,                                                   \
                  "%s",                                                       \
                  __func__)

#define LOGGING_VLOG_SCOPE_FUNCTION(level)                            \
    auto LOGGINGLOG_ANONYMOUS_VARIABLE(msg_context) =                 \
        (static_cast<logging::logger_verbosity_enum>(level) >         \
         logging::logger::get_current_verbosity_cutoff())             \
            ? logging::logger::log_scope_raii()                       \
            : logging::logger::log_scope_raii(                        \
                  static_cast<logging::logger_verbosity_enum>(level), \
                  __FILE__,                                           \
                  __LINE__,                                           \
                  "%s",                                               \
                  __func__)

#define LOGGING_LOG_START_SCOPE(verbosity_name, id) \
    logging::logger::start_scope(                   \
        logging::logger_verbosity_enum::VERBOSITY_##verbosity_name, id, __FILE__, __LINE__)

#define LOGGING_VLOG_START_SCOPE(level, id) \
    logging::logger::start_scope(           \
        static_cast<logging::logger_verbosity_enum>(level), id, __FILE__, __LINE__)

#define LOGGING_LOG_END_SCOPE(id) logging::logger::end_scope(id)

#define LOGGING_LOG_INFO(format_string, ...) LOGGING_LOG(INFO, format_string, ##__VA_ARGS__)

#ifndef NDEBUG
#define LOGGING_LOG_INFO_DEBUG(format_string, ...) LOGGING_LOG_INFO(format_string, ##__VA_ARGS__)
#else
#define LOGGING_LOG_INFO_DEBUG(format_string, ...)
#endif

#define LOGGING_LOG_WARNING(format_string, ...) LOGGING_LOG(WARNING, format_string, ##__VA_ARGS__)
#define LOGGING_LOG_ERROR(format_string, ...) LOGGING_LOG(ERROR, format_string, ##__VA_ARGS__)
#define LOGGING_LOG_FATAL(format_string, ...) LOGGING_LOG(FATAL, format_string, ##__VA_ARGS__)

/**
 * Start / stop a log file at the current verbosity cutoff. `file_name` may be
 * a `const char*` or a `std::string`. Does not change stderr verbosity.
 */
#define START_LOG_TO_FILE(file_name)                                    \
    do                                                                  \
    {                                                                   \
        const std::string _logging_file_path_ = std::string(file_name); \
        if (!_logging_file_path_.empty())                               \
        {                                                               \
            logging::logger::log_to_file(                               \
                _logging_file_path_.c_str(),                            \
                logging::logger::file_mode::truncate,                   \
                logging::logger::get_current_verbosity_cutoff());       \
        }                                                               \
    } while (0)

#define END_LOG_TO_FILE(file_name)                                         \
    do                                                                     \
    {                                                                      \
        const std::string _logging_file_path_ = std::string(file_name);    \
        if (!_logging_file_path_.empty())                                  \
        {                                                                  \
            logging::logger::end_log_to_file(_logging_file_path_.c_str()); \
        }                                                                  \
    } while (0)

#define LOG_TO_FILE_NAME(file_name) (std::string(file_name) + ".log")
#define START_LOG_TO_FILE_NAME(file_name) START_LOG_TO_FILE(LOG_TO_FILE_NAME(file_name))
#define END_LOG_TO_FILE_NAME(file_name) END_LOG_TO_FILE(LOG_TO_FILE_NAME(file_name))
