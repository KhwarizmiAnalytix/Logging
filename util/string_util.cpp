/**
 * @file string_util.cpp
 * @brief Implementation of high-performance string utility functions
 *
 * This file contains optimized implementations of string manipulation,
 * conversion, and utility functions for the XSigma Core library.
 *
 * Key design principles:
 * - Performance-first approach for financial computing applications
 * - Thread-safe implementations where applicable
 * - Locale-independent behavior for consistent results
 * - Robust error handling with clear failure modes
 *
 * @author XSigma Development Team
 * @version 2.0
 * @date 2024
 */

#include "util/string_util.h"

#include "common/logging_macros.h"

// The project's single formatting-backend dependency. Confined to this file
// on purpose: see strings::vformat() below.
#if LOGGING_FORMAT_USE_STD
#if __cplusplus < 202002L
#error "LOGGING_FORMAT_USE_STD=1 needs C++20; build with -DCMAKE_CXX_STANDARD=20 or drop the flag."
#endif
#include <format>
#else
#include <fmt/args.h>
#include <fmt/format.h>
#endif

#include <charconv>  // for to_chars (shortest round-trip floats, where available)
#include <cmath>     // for isfinite
#include <cstdio>    // for snprintf, vsnprintf
#include <cstdlib>   // for strtod, strtof, abs, strtol, free
#include <cstring>   // for strlen, memcpy
#include <limits>    // for numeric_limits
#include <memory>
#include <string>  // for char_traits, string, operator<<, allocator, operator==, oper...
#include <string_view>

#include "util/exception.h"  // for LOGGING_CHECK_DEBUG, LOGGING_CHECK, LOGGING_CHECK_VALUE

// =============================================================================
// PLATFORM-SPECIFIC CONFIGURATION
// =============================================================================

#if LOGGING_HAS_CXA_DEMANGLE
#include <cxxabi.h>
#endif

// =============================================================================
// MAIN LOGGING STRING UTILITIES IMPLEMENTATION
// =============================================================================

namespace logging
{
// Constants for cleaning up demangled names
constexpr std::string_view CLASS_NAME = "class ";  ///< C++ class prefix to remove
constexpr std::string_view SPACE_LIB1 = "__1::";   ///< libstdc++ namespace to remove

/**
 * @brief Implementation of C++ symbol demangling
 * @note This function converts mangled C++ symbols to human-readable names
 * @note Falls back gracefully on platforms without demangling support
 */
std::string demangle(const char* name)
{
    // Handle null or empty input
    if ((name == nullptr) || *name == '\0')
    {
        return "<unknown>";
    }

    std::string ret = name;

#if LOGGING_HAS_CXA_DEMANGLE
    int status = -1;

    // Use GCC/Clang ABI demangling function
    // This converts mangled names like "_Z1gv" to readable names like "g()"
    // Reference: https://github.com/gcc-mirror/gcc/blob/master/libstdc%2B%2B-v3/libsupc%2B%2B/cxxabi.h
    // NOTE: __cxa_demangle returns malloc'd memory that must be freed
    std::unique_ptr<char, decltype(&std::free)> demangled(
        abi::__cxa_demangle(name, nullptr, 0, &status),  // NOLINT - C API
        &std::free);

    // Demangling may fail for symbols that don't follow the standard C++
    // (Itanium ABI) mangling scheme. Examples include 'main', 'clone', etc.
    // In such cases, the original mangled name is a reasonable fallback.
    if (status == 0 && demangled != nullptr)
    {
        ret = demangled.get();
    }
#endif  // LOGGING_HAS_CXA_DEMANGLE

    // Clean up common unwanted prefixes and suffixes for better readability
    erase_all_sub_string(ret, CLASS_NAME);  // Remove "class " prefix
    // Collapse nested-template closing brackets ("> > >" -> ">>>"). One pass only
    // merges adjacent pairs, so repeat until a pass makes no more replacements to
    // correctly handle arbitrarily deep nesting.
    while (replace_all(ret, "> >", ">>") > 0) {}
    erase_all_sub_string(ret, SPACE_LIB1);  // Remove libstdc++ internal namespace

    return ret;
}
// =============================================================================
// STRING MANIPULATION AND REPLACEMENT FUNCTIONS
// =============================================================================

/**
 * @brief Replace all occurrences of substring with another string
 * @note Performs in-place modification for memory efficiency
 * @note Uses iterative approach to handle overlapping replacements correctly
 */
size_t replace_all(std::string& s, const char* from, const char* to)
{
    // Validate input parameters
    LOGGING_CHECK(from && *from, "Source string cannot be null or empty");
    LOGGING_CHECK(to, "Replacement string cannot be null");

    size_t     numReplaced = 0;
    const auto lenFrom     = std::strlen(from);
    const auto lenTo       = std::strlen(to);

    // Find and replace all occurrences
    for (auto pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + lenTo))
    {
        s.replace(pos, lenFrom, to);
        numReplaced++;
    }
    return numReplaced;
}

/**
 * @brief Remove all occurrences of a substring from a string
 * @note More efficient than replace_all when replacing with empty string
 * @note Uses iterative approach to handle multiple occurrences
 * @note Marked noexcept for performance in exception-sensitive contexts
 */
void erase_all_sub_string(std::string& mainStr, std::string_view const& toErase) noexcept
{
    size_t pos;

    // Search for the substring in a loop until nothing is found
    while ((pos = mainStr.find(toErase)) != std::string::npos)
    {
        // Erase the found substring
        mainStr.erase(pos, toErase.length());
    }
}
}  // namespace logging

namespace logging
{

// C++20 compatibility utility for std::string_view::starts_with()
// Note: These functions provide C++20 functionality with C++17 fallback
bool starts_with(std::string_view str, std::string_view prefix)
{
#if __cplusplus >= 202002L
    return str.starts_with(prefix);
#else
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
#endif
}

// C++20 compatibility utility for std::string_view::ends_with()
bool ends_with(std::string_view str, std::string_view suffix)
{
#if __cplusplus >= 202002L
    return str.ends_with(suffix);
#else
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
#endif
}

}  // namespace logging

namespace logging
{
namespace strings
{
namespace
{
// -----------------------------------------------------------------------------
// Shortest round-trip float rendering
//
// std::to_chars is the right tool, but its floating-point overloads are not
// available everywhere the project builds (libc++ on macOS does not define
// __cpp_lib_to_chars), and the obvious substitutes are both wrong: the stream
// default of 6 significant digits truncates (3.14159265358979 -> "3.14159"),
// while max_digits10 over-prints (0.1 -> "0.10000000000000001"). Either one
// would make log messages and check failures differ between platforms.
//
// The emulation below reproduces std::to_chars' plain-format contract: the
// shortest digit string that parses back to the same value, printed fixed or
// scientific -- whichever is shorter, fixed winning ties.
// -----------------------------------------------------------------------------

bool round_trips(const char* text, float value)
{
    return std::strtof(text, nullptr) == value;
}
bool round_trips(const char* text, double value)
{
    return std::strtod(text, nullptr) == value;
}
bool round_trips(const char* text, long double value)
{
    return std::strtold(text, nullptr) == value;
}

int print_scientific(char* buffer, size_t size, int decimals, float value)
{
    return std::snprintf(buffer, size, "%.*e", decimals, static_cast<double>(value));
}
int print_scientific(char* buffer, size_t size, int decimals, double value)
{
    return std::snprintf(buffer, size, "%.*e", decimals, value);
}
int print_scientific(char* buffer, size_t size, int decimals, long double value)
{
    return std::snprintf(buffer, size, "%.*Le", decimals, value);
}

int print_fixed(char* buffer, size_t size, int decimals, float value)
{
    return std::snprintf(buffer, size, "%.*f", decimals, static_cast<double>(value));
}
int print_fixed(char* buffer, size_t size, int decimals, double value)
{
    return std::snprintf(buffer, size, "%.*f", decimals, value);
}
int print_fixed(char* buffer, size_t size, int decimals, long double value)
{
    return std::snprintf(buffer, size, "%.*Lf", decimals, value);
}

template <typename T, typename = void>
struct has_floating_to_chars : std::false_type
{
};

template <typename T>
struct has_floating_to_chars<
    T,
    std::void_t<decltype(std::to_chars(
        static_cast<char*>(nullptr), static_cast<char*>(nullptr), T{}))>> : std::true_type
{
};

template <typename T>
std::string shortest_round_trip_impl(T value)
{
#if !LOGGING_PORTABLE_FLOAT_FORMAT
    // Some standard libraries advertise <charconv> support but still omit
    // certain floating overloads (notably long double). Probe the exact type
    // before instantiating the call so Windows/macOS fall back cleanly.
    if constexpr (has_floating_to_chars<T>::value)
    {
        char       buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec == std::errc())
        {
            return std::string(buffer, static_cast<size_t>(result.ptr - buffer));
        }
    }
#endif
    if (!std::isfinite(value))
    {
        // "inf" / "-inf" / "nan": no digits to shorten, and the round-trip test
        // below never converges for NaN.
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    // Fewest digits after the point in scientific form that still round-trips.
    constexpr int kMaxDecimals = std::numeric_limits<T>::max_digits10;
    char          scientific[64];
    int           decimals = 0;
    for (; decimals <= kMaxDecimals; ++decimals)
    {
        const int written = print_scientific(scientific, sizeof(scientific), decimals, value);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(scientific))
        {
            break;
        }
        if (round_trips(scientific, value))
        {
            break;
        }
    }
    if (decimals > kMaxDecimals)
    {
        decimals = kMaxDecimals;
        print_scientific(scientific, sizeof(scientific), decimals, value);
    }

    // Same digits in fixed form; keep it when it is no longer than scientific.
    const char* exponent_text  = std::strchr(scientific, 'e');
    const int   exponent       = (exponent_text != nullptr)
                                     ? static_cast<int>(std::strtol(exponent_text + 1, nullptr, 10))
                                     : 0;
    const int   fixed_decimals = decimals - exponent;

    char      fixed[512];
    const int written =
        print_fixed(fixed, sizeof(fixed), fixed_decimals > 0 ? fixed_decimals : 0, value);
    if (written > 0 && static_cast<size_t>(written) < sizeof(fixed) &&
        std::strlen(fixed) <= std::strlen(scientific) && round_trips(fixed, value))
    {
        return fixed;
    }
    return scientific;
}
}  // namespace

namespace internal
{
std::string shortest_round_trip(float value)
{
    return shortest_round_trip_impl(value);
}
std::string shortest_round_trip(double value)
{
    return shortest_round_trip_impl(value);
}
std::string shortest_round_trip(long double value)
{
    return shortest_round_trip_impl(value);
}
}  // namespace internal

#if LOGGING_FORMAT_USE_STD
// Minimal {}/{N} substitution used only by the std::format fallback path below.
// Handles the "{{" / "}}" escapes; a placeholder with no matching argument is
// left as written rather than throwing.
std::string substitute_placeholders(
    std::string_view format_str, const std::string* args, size_t count)
{
    std::string result;
    result.reserve(format_str.size() + 16 * count);

    size_t next_arg = 0;
    for (size_t i = 0; i < format_str.size(); ++i)
    {
        const char c = format_str[i];
        if ((c == '{' || c == '}') && i + 1 < format_str.size() && format_str[i + 1] == c)
        {
            result += c;  // "{{" -> "{", "}}" -> "}"
            ++i;
            continue;
        }
        if (c != '{')
        {
            result += c;
            continue;
        }

        const size_t close = format_str.find('}', i + 1);
        if (close == std::string_view::npos)
        {
            result += format_str.substr(i);
            break;
        }

        const std::string_view spec  = format_str.substr(i + 1, close - i - 1);
        size_t                 index = next_arg;
        if (spec.empty())
        {
            ++next_arg;
        }
        else
        {
            index = 0;
            for (const char digit : spec)
            {
                if (digit < '0' || digit > '9')
                {
                    index = count;  // not a plain index: leave the placeholder as-is
                    break;
                }
                index = index * 10 + static_cast<size_t>(digit - '0');
            }
        }

        if (index < count)
        {
            result += args[index];
        }
        else
        {
            result += format_str.substr(i, close - i + 1);
        }
        i = close;
    }
    return result;
}
#endif

// The one place message formatting is implemented, and the only translation
// unit in the project that includes a formatting library: every
// LOGGING_CHECK, LOGGING_THROW and LOGGING_LOG_* message funnels through
// here. Arguments arrive already converted to strings by strings::format(),
// which is what keeps fmt (and <format>) out of every header and out of
// every other .cpp -- callers name no formatting-library type at all.
//
// Switching backends is a change to this function alone; select the C++20
// standard library with -DLOGGING_FORMAT_USE_STD=1. fmt is the default
// because it has a dynamic argument store, which is what a runtime argument
// count needs; std::format has no equivalent (std::make_format_args needs a
// compile-time pack), so that path dispatches on the count instead, and falls
// back to plain placeholder substitution past the counts it enumerates.
std::string vformat(std::string_view format_str, const std::string* args, size_t count)
{
#if LOGGING_FORMAT_USE_STD
    const std::string_view fs = format_str;
    switch (count)
    {
    case 0:
        return std::vformat(fs, std::make_format_args());
    case 1:
        return std::vformat(fs, std::make_format_args(args[0]));
    case 2:
        return std::vformat(fs, std::make_format_args(args[0], args[1]));
    case 3:
        return std::vformat(fs, std::make_format_args(args[0], args[1], args[2]));
    case 4:
        return std::vformat(fs, std::make_format_args(args[0], args[1], args[2], args[3]));
    case 5:
        return std::vformat(fs, std::make_format_args(args[0], args[1], args[2], args[3], args[4]));
    case 6:
        return std::vformat(
            fs, std::make_format_args(args[0], args[1], args[2], args[3], args[4], args[5]));
    case 7:
        return std::vformat(
            fs,
            std::make_format_args(args[0], args[1], args[2], args[3], args[4], args[5], args[6]));
    case 8:
        return std::vformat(
            fs,
            std::make_format_args(
                args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]));
    default:
        break;
    }
    // More arguments than the dispatch can bind. std::vformat would throw
    // std::format_error on the placeholders it has no argument for, so fall back
    // to substituting the placeholders directly. Every argument is already a
    // string by this point, so the only thing lost is format-spec support
    // ({:>8} and friends), which no call site with this many arguments uses.
    return substitute_placeholders(format_str, args, count);
#else
    fmt::dynamic_format_arg_store<fmt::format_context> store;
    store.reserve(count, 0);
    for (size_t i = 0; i < count; ++i)
    {
        store.push_back(args[i]);
    }
    return fmt::vformat(fmt::string_view(format_str.data(), format_str.size()), store);
#endif
}
}  // namespace strings
}  // namespace logging
