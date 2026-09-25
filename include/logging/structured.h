#pragma once

#include <map>
#include <string>
#include <vector>

#include "include/logging/level.h"

namespace logging {

/**
 * Structured logging - emit key-value pairs alongside messages.
 *
 * Enables production-grade structured logging where events are recorded
 * with semantic fields rather than just formatted strings.
 *
 * Usage:
 *   logging::structured_event event("payment_processed");
 *   event.add("user_id", 12345);
 *   event.add("amount", 99.99);
 *   event.add("currency", "USD");
 *   event.add("status", "success");
 *   event.log<level::info>();
 *
 * Or with builder pattern:
 *   logging::structured_event("api_request")
 *       .add("method", "POST")
 *       .add("path", "/api/users")
 *       .add("status_code", 201)
 *       .add("latency_ms", 42)
 *       .log<level::info>();
 *
 * Backends that support structured logging (native with JSON output)
 * will emit these as JSON objects. Others format as key=value strings.
 */

class structured_event {
public:
    explicit structured_event(const char* event_name) : name_(event_name) {}

    // Builder pattern - return self for chaining
    structured_event& add(const char* key, int64_t value)
    {
        fields_[key] = std::to_string(value);
        return *this;
    }

    structured_event& add(const char* key, double value)
    {
        fields_[key] = std::to_string(value);
        return *this;
    }

    structured_event& add(const char* key, const char* value)
    {
        fields_[key] = value ? value : "";
        return *this;
    }

    structured_event& add(const char* key, const std::string& value)
    {
        fields_[key] = value;
        return *this;
    }

    structured_event& add(const char* key, bool value)
    {
        fields_[key] = value ? "true" : "false";
        return *this;
    }

    // Message context
    structured_event& message(const char* msg)
    {
        message_ = msg;
        return *this;
    }

    // Log the event with specified severity
    template <level Level>
    void log(const char* fname = __builtin_FILE(), unsigned line = __builtin_LINE()) const
    {
        emit_structured(Level, fname, line);
    }

    // Convenience methods for common levels
    void info(const char* fname = __builtin_FILE(), unsigned line = __builtin_LINE()) const
    {
        log<level::info>(fname, line);
    }

    void warn(const char* fname = __builtin_FILE(), unsigned line = __builtin_LINE()) const
    {
        log<level::warn>(fname, line);
    }

    void error(const char* fname = __builtin_FILE(), unsigned line = __builtin_LINE()) const
    {
        log<level::error>(fname, line);
    }

    void debug(const char* fname = __builtin_FILE(), unsigned line = __builtin_LINE()) const
    {
        log<level::debug>(fname, line);
    }

    // Access fields
    const std::map<std::string, std::string>& fields() const { return fields_; }
    const std::string& name() const { return name_; }
    const std::string& message() const { return message_; }

private:
    std::string name_;
    std::string message_;
    std::map<std::string, std::string> fields_;

    void emit_structured(level lv, const char* fname, unsigned line) const;
};

/**
 * JSON field builder - for native JSON output backends.
 *
 * Creates properly escaped JSON representation of structured data.
 * Example output:
 *   {"event": "payment_processed", "user_id": 12345, "amount": 99.99}
 */
std::string to_json(const structured_event& event);

/**
 * Key-value format builder - for text backends.
 *
 * Creates key=value representation suitable for line-based logging.
 * Example output:
 *   event=payment_processed user_id=12345 amount=99.99
 */
std::string to_kvpairs(const structured_event& event);

}  // namespace logging
