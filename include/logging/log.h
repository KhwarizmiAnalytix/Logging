#pragma once

#include <fmt/format.h>
#include <string_view>

#include "include/logging/source_location.h"
#include "include/logger/logger_verbosity_enum.h"

namespace logging {

/**
 * Hot-path logging with typed format arguments.
 * Enables zero-copy formatting for capable backends (spdlog).
 *
 * Usage (preferred):
 *   LOGGING_LOG_INFO("value={}", x);  // Uses template dispatch
 *
 * Old usage (still works):
 *   logging::logger::log(level::info, __FILE__, __LINE__, "value=...");
 */

namespace detail {

template <typename... Args>
inline void log_impl(logging::logger_verbosity_enum verbosity,
    const logging::source_location& loc,
    fmt::format_string<Args...> format,
    Args&&... args) {

    // Format message (optimization: spdlog backend could skip this)
    const std::string message = fmt::format(format, std::forward<Args>(args)...);

    // Route through dispatcher
    logging::dispatcher::log(verbosity, loc.file_name, loc.line, message.c_str());
}

}  // namespace detail

/**
 * Template-based log function with format string validation.
 *
 * Example:
 *   logging::log<logging::logger_verbosity_enum::VERBOSITY_INFO>(
 *       logging::source_location::current(),
 *       "User {} logged in with id={}", name, user_id);
 */
template <logging::logger_verbosity_enum Verbosity, typename... Args>
inline void log(const logging::source_location& loc,
    fmt::format_string<Args...> format,
    Args&&... args) {
    detail::log_impl(Verbosity, loc, format, std::forward<Args>(args)...);
}

/**
 * Runtime-level variant (used by traditional macros).
 */
template <typename... Args>
inline void vlog(logging::logger_verbosity_enum verbosity,
    const logging::source_location& loc,
    fmt::format_string<Args...> format,
    Args&&... args) {
    detail::log_impl(verbosity, loc, format, std::forward<Args>(args)...);
}

}  // namespace logging
