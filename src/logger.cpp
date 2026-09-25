#include "include/logger/logger.h"

#include "include/logger/config.h"
#include "include/logger/structured.h"
#include "src/backend/backend.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "include/common/logging_macros.h"
#include "logger_verbosity_enum.h"

// This translation unit is now a thin facade. Backend selection and all
// per-call I/O live behind backend::active_backend() (see src/backend/
// backend.h and src/backend/*_backend.h); the ONLY compile-time backend
// branch is the type alias in backend.h.

namespace logging
{
namespace
{
// printf-style formatting for the *_f / scope_f entry points.
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

// Main-thread name captured by set_thread_name, forwarded to backend init.
char g_main_thread_name[128] = {};

// Named-scope stack: each entry owns the backend scope token, so popping it
// runs the backend's scope-exit action. Kept per-thread.
thread_local std::vector<std::pair<std::string, std::unique_ptr<backend::ActiveBackend::Scope>>>
    g_named_scopes;
}  // namespace

//=============================================================================
// Static members
//=============================================================================
bool                  logger::enable_unsafe_signal_handler = false;
bool                  logger::enable_sigabrt_handler       = false;
bool                  logger::enable_sigbus_handler        = false;
bool                  logger::enable_sigfpe_handler        = false;
bool                  logger::enable_sigill_handler        = false;
bool                  logger::enable_sigint_handler        = false;
bool                  logger::enable_sigsegv_handler       = false;
bool                  logger::enable_sigterm_handler       = false;
logger_verbosity_enum logger::internal_verbosity_level_    = logger_verbosity_enum::VERBOSITY_INFO;

logger::logger() = default;

void logger::set_enable_unsafe_signal_handler(bool enabled)
{
    enable_unsafe_signal_handler = enabled;
}

bool logger::get_enable_unsafe_signal_handler()
{
    return enable_unsafe_signal_handler;
}

//=============================================================================
// Core I/O path — delegated to the active backend
//=============================================================================
void logger::log(
    logger_verbosity_enum verbosity, const char* fname, unsigned int lineno, const char* txt)
{
    backend::active_backend().log(verbosity, fname, lineno, txt);
}

// NOLINTNEXTLINE(modernize-avoid-variadic-functions)
void logger::log_f(logger_verbosity_enum verbosity,
    const char*                          fname,
    unsigned int                         lineno,
    const char*                          format,
    ...)
{
    va_list vlist;
    va_start(vlist, format);
    const std::string formatted = vformat_printf(format, vlist);
    va_end(vlist);
    logger::log(verbosity, fname, lineno, formatted.c_str());
}

logger_verbosity_enum logger::get_current_verbosity_cutoff()
{
    return backend::active_backend().get_cutoff();
}

void logger::set_stderr_verbosity(logger_verbosity_enum level)
{
    backend::active_backend().set_stderr_verbosity(level);
}

void logger::set_internal_verbosity_level(logger_verbosity_enum level)
{
    logger::internal_verbosity_level_ = level;
    backend::active_backend().set_internal_verbosity(level);
}

void logger::set_console_mode(bool enabled)
{
    backend::active_backend().set_console_mode(enabled);
}

bool logger::get_console_mode()
{
    return backend::active_backend().get_console_mode();
}

void logger::log_to_file(const char* path, logger::file_mode mode, logger_verbosity_enum verbosity)
{
    backend::active_backend().log_to_file(path, mode, verbosity);
}

void logger::end_log_to_file(const char* path)
{
    backend::active_backend().end_log_to_file(path);
}

void logger::flush()
{
    backend::active_backend().flush();
}

void logger::set_thread_name(const std::string& name)
{
    std::strncpy(g_main_thread_name, name.c_str(), sizeof(g_main_thread_name) - 1);
    g_main_thread_name[sizeof(g_main_thread_name) - 1] = '\0';
    backend::active_backend().set_thread_name(name);
}

std::string logger::get_thread_name()
{
    return backend::active_backend().get_thread_name();
}

void logger::add_callback(const char* id,
    logger::log_handler_callback_t    callback,
    void*                             user_data,
    logger_verbosity_enum             verbosity,
    logger::close_handler_callback_t  on_close,
    logger::flush_handler_callback_t  on_flush)
{
    backend::active_backend().add_callback(id, callback, user_data, verbosity, on_close, on_flush);
}

bool logger::remove_callback(const char* id)
{
    return backend::active_backend().remove_callback(id);
}

bool logger::is_enabled()
{
    // A backend is always selected at compile time (factory.cpp #errors otherwise).
    return true;
}

//=============================================================================
// Scopes
//=============================================================================
class logger::log_scope_raii::ls_internals
{
public:
    std::unique_ptr<backend::ActiveBackend::Scope> state;
};

logger::log_scope_raii::log_scope_raii()                                             = default;
logger::log_scope_raii::log_scope_raii(log_scope_raii&&) noexcept                    = default;
logger::log_scope_raii& logger::log_scope_raii::operator=(log_scope_raii&&) noexcept = default;
logger::log_scope_raii::~log_scope_raii()                                            = default;

// NOLINTNEXTLINE(modernize-avoid-variadic-functions)
logger::log_scope_raii::log_scope_raii(logger_verbosity_enum verbosity,
    const char*                                              fname,
    unsigned int                                             lineno,
    const char*                                              format,
    ...)
{
    va_list vlist;
    va_start(vlist, format);
    const std::string formatted = vformat_printf(format, vlist);
    va_end(vlist);

    internals_        = std::make_unique<ls_internals>();
    internals_->state = backend::active_backend().scope_enter(verbosity, fname, lineno, formatted);
}

void logger::start_scope(
    logger_verbosity_enum verbosity, const char* id, const char* fname, unsigned int lineno)
{
    const std::string label = (id != nullptr) ? id : "";
    g_named_scopes.emplace_back(
        label, backend::active_backend().scope_enter(verbosity, fname, lineno, label));
}

void logger::end_scope(const char* id)
{
    if (g_named_scopes.empty())
    {
        logger::log(logger_verbosity_enum::VERBOSITY_ERROR,
            __FILE__,
            __LINE__,
            fmt::format("Mismatched scope! stack empty, got ({})", id ? id : "").c_str());
        return;
    }
    if (id != nullptr && g_named_scopes.back().first == id)
    {
        g_named_scopes.pop_back();  // token destructor emits scope exit
        return;
    }
    logger::log(logger_verbosity_enum::VERBOSITY_ERROR,
        __FILE__,
        __LINE__,
        fmt::format(
            "Mismatched scope! expected ({}), got ({})", g_named_scopes.back().first, id ? id : "")
            .c_str());
}

// NOLINTNEXTLINE(modernize-avoid-variadic-functions)
void logger::start_scope_f(logger_verbosity_enum verbosity,
    const char*                                  id,
    const char*                                  fname,
    unsigned int                                 lineno,
    const char*                                  format,
    ...)
{
    va_list vlist;
    va_start(vlist, format);
    const std::string formatted = vformat_printf(format, vlist);
    va_end(vlist);
    const std::string label = (id != nullptr) ? id : "";
    g_named_scopes.emplace_back(
        label, backend::active_backend().scope_enter(verbosity, fname, lineno, formatted));
}

//=============================================================================
// Initialization (facade orchestrates; backend performs its own library setup)
//=============================================================================
void logger::init(int& argc, char* argv[], const char* verbosity_flag)
{
    if (argc == 0)
    {
        logger::init();
        return;
    }

    // Command-line verbosity parsing lives in the facade, uniform across backends.
    if (verbosity_flag != nullptr && argv != nullptr)
    {
        for (int i = 1; i < argc - 1; ++i)
        {
            if (std::string(argv[i]) == verbosity_flag)
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

    backend::init_options options;
    options.verbosity_flag   = verbosity_flag;
    options.main_thread_name = (std::strlen(g_main_thread_name) > 0) ? g_main_thread_name : nullptr;
    options.unsafe_signal_handler = enable_unsafe_signal_handler;
    options.sigabrt               = enable_sigabrt_handler;
    options.sigbus                = enable_sigbus_handler;
    options.sigfpe                = enable_sigfpe_handler;
    options.sigill                = enable_sigill_handler;
    options.sigint                = enable_sigint_handler;
    options.sigsegv               = enable_sigsegv_handler;
    options.sigterm               = enable_sigterm_handler;

    backend::active_backend().on_init(argc, argv, options);
}

void logger::init()
{
    int                  argc  = 1;
    std::array<char, 1>  dummy = {'\0'};
    std::array<char*, 2> argv  = {dummy.data(), nullptr};
    logger::init(argc, argv.data());
}

void logger::init(const logging::config& cfg)
{
    set_stderr_verbosity(cfg.level);
    set_console_mode(cfg.console);

    if (cfg.signals.enabled)
    {
        set_enable_unsafe_signal_handler(true);
        enable_sigabrt_handler = cfg.signals.sigabrt;
        enable_sigbus_handler  = cfg.signals.sigbus;
        enable_sigfpe_handler  = cfg.signals.sigfpe;
        enable_sigill_handler  = cfg.signals.sigill;
        enable_sigint_handler  = cfg.signals.sigint;
        enable_sigsegv_handler = cfg.signals.sigsegv;
        enable_sigterm_handler = cfg.signals.sigterm;
    }

    if (cfg.thread_name)
    {
        set_thread_name(cfg.thread_name);
    }

    logger::init();
}

//=============================================================================
// Verbosity conversions
//=============================================================================
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
    std::transform(upper.begin(),
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

//=============================================================================
// Structured logging
//=============================================================================
void structured_event::emit_structured(logger_verbosity_enum lv, const char* fname, unsigned line) const
{
    std::string output = fmt::format("event={}", name_);
    if (!message_.empty())
    {
        output += fmt::format(" message=\"{}\"", message_);
    }
    for (const auto& [key, value] : fields_)
    {
        std::string escaped_value = value;
        size_t      pos           = 0;
        while ((pos = escaped_value.find('"', pos)) != std::string::npos)
        {
            escaped_value.replace(pos, 1, "\\\"");
            pos += 2;
        }
        output += fmt::format(" {}=\"{}\"", key, escaped_value);
    }
    logger::log(lv, fname, line, output.c_str());
}

std::string to_json(const structured_event& event)
{
    std::string json = "{";
    json += fmt::format("\"event\":\"{}\"", event.name());

    if (!event.message().empty())
    {
        json += fmt::format(",\"message\":\"{}\"", event.message());
    }

    for (const auto& [key, value] : event.fields())
    {
        char* end;
        strtod(value.c_str(), &end);
        bool is_number = (*end == '\0' && !value.empty());

        if (is_number || value == "true" || value == "false")
        {
            json += fmt::format(",\"{}\":{}", key, value);
        }
        else
        {
            std::string escaped = value;
            size_t      pos     = 0;
            while ((pos = escaped.find('"', pos)) != std::string::npos)
            {
                escaped.replace(pos, 1, "\\\"");
                pos += 2;
            }
            json += fmt::format(",\"{}\":\"{}\"", key, escaped);
        }
    }
    json += "}";
    return json;
}

std::string to_kvpairs(const structured_event& event)
{
    std::string output = fmt::format("event={}", event.name());
    if (!event.message().empty())
    {
        output += fmt::format(" message=\"{}\"", event.message());
    }
    for (const auto& [key, value] : event.fields())
    {
        output += fmt::format(" {}=\"{}\"", key, value);
    }
    return output;
}

}  // namespace logging
