
#ifndef LOGGING_SOURCE_LOCATION_H
#define LOGGING_SOURCE_LOCATION_H

#include <cstdint>

namespace logging {

/**
 * Source code location information for log records.
 * Captures file, line, and function name for structured logging.
 */
struct source_location {
    const char* file_name = nullptr;
    uint32_t line = 0;
    const char* function_name = nullptr;

    static constexpr source_location current(
        const char* file = __builtin_FILE(),
        uint32_t line = __builtin_LINE(),
        const char* func = __builtin_FUNCTION()) noexcept {
        return source_location{file, line, func};
    }
};

}  // namespace logging
#endif  // LOGGING_SOURCE_LOCATION_H
