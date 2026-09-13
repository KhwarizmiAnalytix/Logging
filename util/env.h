#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "common/logging_export.h"

namespace logging::utils
{

// Set an environment variable.
LOGGING_API void set_env(const char* name, const char* value, bool overwrite = true);

// Checks an environment variable is set.
LOGGING_API bool has_env(const char* name) noexcept;

// Reads an environment variable and returns
// - std::optional<true>,              if set equal to "1"
// - std::optional<false>,             if set equal to "0"
// - nullopt,   otherwise
//
// NB:
// Issues a warning if the value of the environment variable is not 0 or 1.
LOGGING_API std::optional<bool> check_env(const char* name);

// Reads the value of an environment variable if it is set.
// However, check_env should be used if the value is assumed to be a flag.
LOGGING_API std::optional<std::string> get_env(const char* name) noexcept;

// Read a boolean environment variable in a TensorFlow-compatible way.
// Returns true on success and writes the parsed value; otherwise leaves `value`
// as the default and returns false.
LOGGING_API bool read_env_bool(const char* name, bool default_val, bool* value);

// Read an int64 environment variable with whitespace trimming.
// Returns true on success and writes the parsed value; otherwise leaves `value`
// as the default and returns false.
LOGGING_API bool read_env_int64(const char* name, int64_t default_val, int64_t* value);

}  // namespace logging::utils
