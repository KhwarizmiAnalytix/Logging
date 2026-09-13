#pragma once

namespace logging
{
//inline with loguru's verbosity levels
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
}  // namespace logging