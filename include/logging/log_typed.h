
#ifndef LOGGING_LOG_TYPED_H
#define LOGGING_LOG_TYPED_H

#include <fmt/format.h>

#include <string>
#include <utility>

#include "include/logging/level.h"
#include "include/logging/logger.h"
#include "include/logging/source_location.h"

namespace logging
{

/**
 * Hot-path typed logging with compile-time-validated format strings.
 *
 * The source location comes first so it precedes the variadic pack; this is
 * the only ordering that deduces cleanly and lets callers use the library's
 * C++17 `logging::source_location` (never C++20 `std::source_location`, which
 * the library does not require).
 *
 * Usage:
 *   logging::log_typed<level::info>(
 *       logging::source_location::current(), "NPV = {}", npv);
 *
 * Disabled levels are rejected before the arguments are formatted, so
 * expensive expressions in the argument list are only evaluated when the
 * record will actually be emitted. Fatal/critical always pass, matching the
 * single should_log()/is_fatal() gate used everywhere else.
 */
template <level Level, typename... Args>
inline void log_typed(
    const source_location& loc, fmt::format_string<Args...> format, Args&&... args)
{
    if (is_fatal(Level) || should_log(Level, logger::get_current_verbosity_cutoff()))
    {
        std::string message = fmt::format(format, std::forward<Args>(args)...);
        logger::log(Level, loc.file_name, loc.line, message.c_str());
    }
}

// Convenience wrappers keep the location first for the same deduction reason.
template <typename... Args>
inline void trace(const source_location& loc, fmt::format_string<Args...> format, Args&&... args)
{
    log_typed<level::trace>(loc, format, std::forward<Args>(args)...);
}

template <typename... Args>
inline void debug(const source_location& loc, fmt::format_string<Args...> format, Args&&... args)
{
    log_typed<level::debug>(loc, format, std::forward<Args>(args)...);
}

template <typename... Args>
inline void info(const source_location& loc, fmt::format_string<Args...> format, Args&&... args)
{
    log_typed<level::info>(loc, format, std::forward<Args>(args)...);
}

template <typename... Args>
inline void warn(const source_location& loc, fmt::format_string<Args...> format, Args&&... args)
{
    log_typed<level::warn>(loc, format, std::forward<Args>(args)...);
}

template <typename... Args>
inline void error(const source_location& loc, fmt::format_string<Args...> format, Args&&... args)
{
    log_typed<level::error>(loc, format, std::forward<Args>(args)...);
}

template <typename... Args>
inline void critical(const source_location& loc, fmt::format_string<Args...> format, Args&&... args)
{
    log_typed<level::critical>(loc, format, std::forward<Args>(args)...);
}

}  // namespace logging
#endif  // LOGGING_LOG_TYPED_H
