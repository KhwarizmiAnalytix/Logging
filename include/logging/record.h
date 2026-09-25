#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include "logging/level.h"
#include "logging/source_location.h"

namespace logging {

/**
 * Structured key-value field for logging.
 * Each field carries a typed value that backends can serialize.
 */
struct field {
    std::string_view key;
    std::variant<
        int64_t,                // integers
        double,                 // floating-point
        std::string_view,      // strings
        bool                   // booleans
    > value;
};

/**
 * Log record: structured representation of a log event.
 * Contains all metadata, message, and typed fields for backend processing.
 */
struct log_record {
    level severity = level::info;
    std::chrono::system_clock::time_point timestamp;
    source_location location;

    uint64_t thread_id = 0;
    std::string_view thread_name;

    std::string message;
    std::span<const field> fields;
};

/**
 * Legacy Message structure for callback compatibility.
 * Keeps owned-string guarantee for user code that retains messages.
 */
struct Message {
    level severity{level::info};
    std::string filename;
    unsigned line{0};
    std::string preamble;
    std::string indentation;
    std::string prefix;
    std::string message;
};

}  // namespace logging
