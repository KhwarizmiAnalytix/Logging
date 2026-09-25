
#ifndef LOGGING_LEVEL_H
#define LOGGING_LEVEL_H

namespace logging
{

/**
 * Log severity levels. Library-owned semantics; each backend adapter
 * (spdlog, loguru, glog, native) maps these to its own numbering.
 * Ordered from least to most severe.
 */
enum class level : int
{
    trace    = 0,
    debug    = 1,
    info     = 2,
    warn     = 3,
    error    = 4,
    critical = 5,
    off      = 6,
};

/**
 * Canonical severity gate. This is the ONLY place severity comparisons
 * should be written; every macro, template, and backend adapter routes
 * through it so the ordering can never drift again.
 *
 * A message is emitted when its severity is at least the threshold, and
 * the threshold is not `off`. With the conventional ordering above,
 * larger value == more severe, so at threshold `info` the warn/error/
 * critical messages pass and debug/trace are dropped.
 */
constexpr bool should_log(level message, level threshold) noexcept
{
    return threshold != level::off && static_cast<int>(message) >= static_cast<int>(threshold);
}

/**
 * Fatal-class severities must never be filtered: they carry a program
 * abort and side effects a caller relies on, so they bypass the threshold.
 */
constexpr bool is_fatal(level message) noexcept
{
    return message == level::critical;
}

// Deprecated alias for backward compatibility during transition.
// Maps old Loguru-derived VERBOSITY_* names to library-owned level values.
namespace deprecated
{
enum class logger_verbosity_enum : int
{
    VERBOSITY_INVALID = -10,
    VERBOSITY_OFF     = static_cast<int>(level::off),
    VERBOSITY_FATAL   = static_cast<int>(level::critical),
    VERBOSITY_ERROR   = static_cast<int>(level::error),
    VERBOSITY_WARNING = static_cast<int>(level::warn),
    VERBOSITY_INFO    = static_cast<int>(level::info),
    VERBOSITY_TRACE   = static_cast<int>(level::trace),
    VERBOSITY_MAX     = static_cast<int>(level::trace),
};
}  // namespace deprecated

}  // namespace logging
#endif  // LOGGING_LEVEL_H
