
#ifndef LOGGING_LOGGER_LOGGER_VERBOSITY_ENUM_H
#define LOGGING_LOGGER_LOGGER_VERBOSITY_ENUM_H

namespace logging
{
// inline with loguru's verbosity levels
enum class logger_verbosity_enum : int
{
    VERBOSITY_INVALID = -10,
    VERBOSITY_OFF     = -9,
    VERBOSITY_FATAL   = -3,
    VERBOSITY_ERROR   = -2,
    VERBOSITY_WARNING = -1,
    VERBOSITY_INFO    = 0,
    VERBOSITY_TRACE   = +9,
    VERBOSITY_MAX     = +9,
};

/**
 * Canonical severity gate. This is the ONLY place severity comparisons
 * should be written; every macro and backend adapter routes through it.
 *
 * Loguru numbering is inverted from the conventional ordering: lower (more
 * negative) is MORE severe, so a message is emitted when it is at least as
 * severe as the threshold, i.e. message <= threshold, and the threshold is
 * not OFF.
 */
constexpr bool should_log(logger_verbosity_enum message, logger_verbosity_enum threshold) noexcept
{
    return threshold != logger_verbosity_enum::VERBOSITY_OFF && message <= threshold;
}

/**
 * Fatal-class severities must never be filtered: they carry a program abort
 * and side effects a caller relies on, so they bypass the threshold.
 */
constexpr bool is_fatal(logger_verbosity_enum message) noexcept
{
    return message == logger_verbosity_enum::VERBOSITY_FATAL;
}
}  // namespace logging
#endif  // LOGGING_LOGGER_LOGGER_VERBOSITY_ENUM_H
