#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "include/logging/level.h"

namespace logging {

/**
 * Extended log record with metadata - future replacement for Message.
 *
 * Provides structured access to log entry details including timestamp,
 * thread information, source location, and field-value pairs.
 *
 * Backward compatible: Message struct remains unchanged; LogRecord
 * is provided for new code that needs extended metadata.
 */

struct field {
    std::string key;
    std::string value;
};

struct LogRecord {
    // Severity and content
    level severity = level::info;
    std::string message;

    // Source location
    std::string filename;
    unsigned line = 0;

    // Timing
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();

    // Thread information
    unsigned long thread_id = 0;
    std::string thread_name;

    // Formatted output (built by logger)
    std::string preamble;      // [timestamp] [severity]
    std::string prefix;        // [thread] [function]
    std::string indentation;   // Indent for continuations

    // Structured fields (Phase D+)
    std::vector<field> fields;

    // Backward compatibility accessor
    unsigned long get_timestamp_ms() const {
        auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(timestamp);
        return ms.time_since_epoch().count();
    }
};

}  // namespace logging
