#pragma once

#include <fmt/format.h>

#include "include/logging/level.h"
#include "include/logger/logger.h"

namespace logging {

/**
 * Hot-path typed logging - passes format arguments directly to backend.
 * Uses fmt for compile-time format string validation.
 *
 * Usage:
 *   logging::log_typed<level::info>("NPV = {}", npv);
 *   logging::log_typed<level::warn>("Failed: {}", reason);
 *
 * Advantages over LOGGING_LOG_INFO macros:
 * - Type-checked format strings at compile time (via fmt)
 * - No macro boilerplate
 * - Direct template dispatch (cleaner than macro expansion)
 *
 * Note: Disabled logs check cutoff before evaluating arguments,
 * supporting lazy evaluation of expensive expressions.
 */

template <level Level, typename... Args>
inline void log_typed(fmt::format_string<Args...> format,
    Args&&... args,
    const char*   fname = __builtin_FILE(),
    unsigned int  lineno = __builtin_LINE())
{
    if (static_cast<int>(Level) <= static_cast<int>(logger::get_current_verbosity_cutoff()))
    {
        std::string message = fmt::format(format, std::forward<Args>(args)...);
        logger::log(Level, fname, lineno, message.c_str());
    }
}

// Convenience aliases for common levels
template <typename... Args>
inline void trace(fmt::format_string<Args...> format,
    Args&&... args,
    const std::source_location& loc = std::source_location::current())
{
    log_typed<level::trace>(format, std::forward<Args>(args)..., loc);
}

template <typename... Args>
inline void debug(fmt::format_string<Args...> format,
    Args&&... args,
    const std::source_location& loc = std::source_location::current())
{
    log_typed<level::debug>(format, std::forward<Args>(args)..., loc);
}

template <typename... Args>
inline void info(fmt::format_string<Args...> format,
    Args&&... args,
    const std::source_location& loc = std::source_location::current())
{
    log_typed<level::info>(format, std::forward<Args>(args)..., loc);
}

template <typename... Args>
inline void warn(fmt::format_string<Args...> format,
    Args&&... args,
    const std::source_location& loc = std::source_location::current())
{
    log_typed<level::warn>(format, std::forward<Args>(args)..., loc);
}

template <typename... Args>
inline void error(fmt::format_string<Args...> format,
    Args&&... args,
    const std::source_location& loc = std::source_location::current())
{
    log_typed<level::error>(format, std::forward<Args>(args)..., loc);
}

template <typename... Args>
inline void critical(fmt::format_string<Args...> format,
    Args&&... args,
    const std::source_location& loc = std::source_location::current())
{
    log_typed<level::critical>(format, std::forward<Args>(args)..., loc);
}

}  // namespace logging
