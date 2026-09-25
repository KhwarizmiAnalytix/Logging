#pragma once

namespace logging {

/**
 * Log severity levels. Library-owned semantics; each backend adapter
 * (spdlog, loguru, glog, native) maps these to its own numbering.
 * Ordered from least to most severe.
 */
enum class level : int
{
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    error = 4,
    critical = 5,
    off = 6,
};

// Deprecated alias for backward compatibility during transition.
// Maps old Loguru-derived VERBOSITY_* names to library-owned level values.
namespace deprecated {
enum class logger_verbosity_enum : int
{
    VERBOSITY_INVALID = -10,
    VERBOSITY_OFF = static_cast<int>(level::off),
    VERBOSITY_FATAL = static_cast<int>(level::critical),
    VERBOSITY_ERROR = static_cast<int>(level::error),
    VERBOSITY_WARNING = static_cast<int>(level::warn),
    VERBOSITY_INFO = static_cast<int>(level::info),
    VERBOSITY_TRACE = static_cast<int>(level::trace),
    VERBOSITY_MAX = static_cast<int>(level::trace),
};
}  // namespace deprecated

}  // namespace logging
