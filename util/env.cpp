#include "util/env.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <shared_mutex>

#include "logger/logger.h"
#include "util/exception.h"

namespace logging::utils
{

static std::shared_mutex& get_env_mutex()
{
    static std::shared_mutex env_mutex;
    return env_mutex;
}

// Set an environment variable.
void set_env(const char* name, const char* value, bool overwrite)
{
    std::scoped_lock const lk(get_env_mutex());
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    if (!overwrite)
    {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (std::getenv(name) != nullptr)
        {
            return;
        }
    }
    auto full_env_variable = fmt::format("{}={}", name, value);
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    auto err = _putenv(full_env_variable.c_str());
    LOGGING_CHECK(err == 0, "putenv failed for environment \"{}\", the error is: {}", name, err);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#else
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    auto err = setenv(name, value, static_cast<int>(overwrite));
    LOGGING_CHECK(err == 0, "setenv failed for environment \"{}\", the error is: {}", name, err);
#endif
}

// Reads an environment variable and returns the content if it is set
std::optional<std::string> get_env(const char* name) noexcept
{
    std::shared_lock const lk(get_env_mutex());
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    auto* envar = std::getenv(name);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    if (envar != nullptr)
    {
        return std::string(envar);
    }
    return std::nullopt;
}

// Checks an environment variable is set.
bool has_env(const char* name) noexcept
{
    return get_env(name).has_value();
}

// Reads an environment variable and returns
// - optional<true>,              if set equal to "1"
// - optional<false>,             if set equal to "0"
// - nullopt,   otherwise
//
// NB:
// Issues a warning if the value of the environment variable is not 0 or 1.
std::optional<bool> check_env(const char* name)
{
    auto env_opt = get_env(name);
    if (env_opt.has_value())
    {
        if (env_opt == "0")
        {
            return false;
        }
        if (env_opt == "1")
        {
            return true;
        }
        LOGGING_LOG_WARNING(
            "Ignoring invalid value for boolean flag {}: {}. Valid values are 0 or 1.",
            name,
            *env_opt);
    }
    return std::nullopt;
}

bool read_env_bool(const char* name, bool default_val, bool* value)
{
    std::string const env_name = name;
    *value                     = default_val;
    auto env_opt               = get_env(name);
    if (!env_opt.has_value())
    {
        return true;
    }

    std::string str_value = *env_opt;
    std::transform(
        str_value.begin(),
        str_value.end(),
        str_value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (str_value == "0" || str_value == "false")
    {
        *value = false;
        return true;
    }
    if (str_value == "1" || str_value == "true")
    {
        *value = true;
        return true;
    }

    LOGGING_LOG_ERROR(
        "Failed to parse the env-var {} into bool: {}. Use the default value: {}",
        env_name,
        str_value,
        default_val);
    return false;
}

bool read_env_int64(const char* name, int64_t default_val, int64_t* value)
{
    std::string const env_name = name;
    *value                     = default_val;
    auto env_opt               = get_env(name);
    if (!env_opt.has_value())
    {
        return true;
    }

    std::string str   = *env_opt;
    auto        start = str.find_first_not_of(" \t\n\r");
    auto        end   = str.find_last_not_of(" \t\n\r");
    if (start == std::string::npos || end == std::string::npos)
    {
        LOGGING_LOG_ERROR(
            "Failed to parse the env-var {} into int64: {}. Use the default value: {}",
            env_name,
            str,
            default_val);
        return false;
    }
    str = str.substr(start, end - start + 1);

    try
    {
        size_t        pos = 0;
        int64_t const val = std::stoll(str, &pos);
        if (pos == str.length())
        {
            *value = val;
            return true;
        }
    }
    catch (std::exception const& e)
    {
        LOGGING_LOG_ERROR(
            "Failed to parse the env-var {} into int64: {}. Use the default value: {} ({})",
            env_name,
            str,
            default_val,
            e.what());
        return false;
    }

    LOGGING_LOG_ERROR(
        "Failed to parse the env-var {} into int64: {}. Use the default value: {}",
        env_name,
        str,
        default_val);
    return false;
}
}  // namespace logging::utils
