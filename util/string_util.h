/**
 * @file string_util.h
 * @brief Comprehensive string utility functions for the XSigma Core library
 *
 * This header provides a collection of high-performance string manipulation,
 * conversion, and utility functions designed for financial computing applications.
 * All functions are optimized for performance and thread-safety where applicable.
 *
 * @author XSigma Development Team
 * @version 2.0
 * @date 2024
 */

#pragma once

#include <stdarg.h>  // for va_list

#include <algorithm>  // for transform
#include <cctype>
#include <cstddef>      // for size_t
#include <cstdint>      // for int64_t, uint64_t, uint32_t, int32_t
#include <filesystem>   // for path (C++17)
#include <iomanip>      // for setfill, setw, hex
#include <sstream>      // for ostream, ostringstream, stringstream
#include <string>       // for string, allocator, char_traits, stoi, to_string
#include <string_view>  // for string_view
#include <type_traits>  // for is_floating_point
#include <typeinfo>
#include <vector>  // for vector

#include "common/logging_export.h"  // for LOGGING_API, LOGGING_VISIBILITY
#include "common/logging_macros.h"  // LOGGING_FORCE_INLINE

// =============================================================================
// ENUM CONVERSION UTILITIES
// =============================================================================

#if LOGGING_HAS_MAGICENUM
#include <magic_enum/magic_enum.hpp>

namespace logging
{
/**
 * @brief Convert an enum value to its string representation using magic_enum
 * @tparam E The enum type
 * @param x The enum value to convert
 * @return String view containing the enum name
 * @note This function uses magic_enum for reflection-based enum conversion
 * @example
 * @code
 * enum class Color { Red, Green, Blue };
 * auto name = enum_to_string(Color::Red); // Returns "Red"
 * @endcode
 */
template <typename E>
inline std::string enum_to_string(E x)
{
    return std::string(magic_enum::enum_name(x));
}

/**
 * @brief Convert a string to an enum value using magic_enum (case-insensitive)
 * @tparam E The enum type
 * @param str The string to convert
 * @return The corresponding enum value
 * @throws std::bad_optional_access if the string doesn't match any enum value
 * @note This function performs case-insensitive matching
 * @example
 * @code
 * enum class Color { Red, Green, Blue };
 * auto color = string_to_enum<Color>("red"); // Returns Color::Red
 * @endcode
 */
template <typename E>
E string_to_enum(std::string_view str)
{
    return magic_enum::enum_cast<E>(str, magic_enum::case_insensitive).value();
}
}  // namespace logging
#else
namespace logging
{
/**
 * @brief Convert an enum value to its numeric string representation (fallback)
 * @tparam E The enum type
 * @param x The enum value to convert
 * @return String containing the numeric representation
 * @note This is a fallback implementation when magic_enum is not available
 */
template <typename E>
inline std::string enum_to_string(E x)
{
    return std::to_string(static_cast<int>(x));
}

/**
 * @brief Convert a string to an enum value using numeric conversion (fallback)
 * @tparam E The enum type
 * @param str The string containing numeric value
 * @return The corresponding enum value
 * @throws std::invalid_argument if the string is not a valid integer
 * @note This is a fallback implementation when magic_enum is not available
 */
template <typename E>
E string_to_enum(std::string_view str)
{
    return static_cast<E>(std::stoi(std::string(str)));
}
}  // namespace logging
#endif

// =============================================================================
// TYPE INTROSPECTION AND DEMANGLING UTILITIES
// =============================================================================

namespace logging
{
/**
 * @brief Demangle a C++ symbol name to human-readable format
 * @param name The mangled symbol name (typically from typeid().name())
 * @return Demangled string representation of the symbol
 * @note On platforms without demangling support, returns the original name
 * @note The function handles null/empty input gracefully
 * @example
 * @code
 * const char* mangled = typeid(std::vector<int>).name();
 * std::string readable = demangle(mangled); // Returns "std::vector<int>"
 * @endcode
 */
LOGGING_API std::string demangle(const char* name);

/**
 * @brief Get the printable name of a type using RTTI
 * @tparam T The type to get the name for
 * @return C-string containing the demangled type name
 * @note Uses static storage for performance - the returned pointer remains valid
 * @note Returns a descriptive message when RTTI is disabled
 * @warning The returned pointer should not be freed
 * @example
 * @code
 * const char* name = demangle_type<std::vector<double>>();
 * // Returns "std::vector<double>" or similar
 * @endcode
 */
template <typename T>
inline const char* demangle_type()
{
#ifdef __GXX_RTTI
    static const std::string name = demangle(typeid(T).name());
    return name.c_str();
#else   // __GXX_RTTI
    return "(RTTI disabled, cannot show name)";
#endif  // __GXX_RTTI
}

// =============================================================================
// SOURCE LOCATION AND DEBUGGING UTILITIES
// =============================================================================

/**
 * @brief Represents a location in source code for debugging purposes
 * @note Used primarily in assertion macros and error reporting
 * @note All members are const char* for minimal memory overhead
 */
struct LOGGING_VISIBILITY source_location
{
    const char* function;  ///< Function name where the location was captured
    const char* file;      ///< Source file name
    int         line;      ///< Line number in the source file
};
/**
 * @brief Replace all occurrences of a substring with another string
 * @param s The string to modify (modified in-place)
 * @param from The substring to find and replace
 * @param to The replacement string
 * @return Number of replacements performed
 * @pre from must not be null or empty
 * @pre to must not be null
 * @note Modifies the input string in-place for performance
 * @example
 * @code
 * std::string text = "Hello world, world!";
 * size_t count = replace_all(text, "world", "universe");
 * // text becomes "Hello universe, universe!", count is 2
 * @endcode
 */
LOGGING_API size_t replace_all(std::string& s, const char* from, const char* to);

LOGGING_API void erase_all_sub_string(
    std::string& mainStr, std::string_view const& toErase) noexcept;
// =============================================================================
// C++20 COMPATIBILITY UTILITIES
// =============================================================================

/**
 * @brief Check if a string starts with a given prefix (C++20 compatibility)
 * @param str The string to check
 * @param prefix The prefix to look for
 * @return true if str starts with prefix, false otherwise
 * @note Uses C++20 std::string::starts_with() when available, C++17 fallback otherwise
 * @note Performs exact character matching (case-sensitive)
 * @example
 * @code
 * bool result = starts_with("Hello World", "Hello"); // Returns true
 * bool result2 = starts_with("Hello World", "hello"); // Returns false
 * @endcode
 */
LOGGING_API bool starts_with(std::string_view str, std::string_view prefix);

/**
 * @brief Check if a string ends with a given suffix (C++20 compatibility)
 * @param str The string to check
 * @param suffix The suffix to look for
 * @return true if str ends with suffix, false otherwise
 * @note Uses C++20 std::string::ends_with() when available, C++17 fallback otherwise
 * @note Performs exact character matching (case-sensitive)
 * @example
 * @code
 * bool result = ends_with("document.pdf", ".pdf"); // Returns true
 * bool result2 = ends_with("document.pdf", ".PDF"); // Returns false
 * @endcode
 */
LOGGING_API bool ends_with(std::string_view str, std::string_view suffix);

}  // namespace logging

namespace logging
{
// =============================================================================
// STRING CONCATENATION UTILITIES
// =============================================================================

namespace strings
{

/**
 * @brief Substitute pre-converted arguments into "{}" placeholders
 *
 * The single point in the project where message formatting is implemented
 * (string_util.cpp). Taking the arguments already converted to strings keeps
 * every formatting library out of this header -- and therefore out of every
 * header and translation unit that uses LOGGING_CHECK / LOGGING_THROW /
 * LOGGING_LOG_*, such as Core's intrusive_ptr.h. Swapping the implementation
 * for fmt::vformat or std::vformat is a change to that one function only.
 *
 * @param format_str Format string whose "{}" placeholders are substituted
 * @param args Arguments, already converted to strings (may be null if count is 0)
 * @param count Number of arguments in args
 * @return The formatted string
 */
LOGGING_API std::string vformat(std::string_view format_str, const std::string* args, size_t count);

/**
 * @brief Format a message by substituting "{}" placeholders in order
 *
 * Supports exactly one feature: a "{}" placeholder per argument, filled in
 * argument order. There are no format specifiers ({:x}, {:.2f}) and no
 * indexed or named placeholders ({0}, {name}) -- "{}" is all any call site
 * in this project uses. Arguments are converted with operator<<, the same
 * conversion str_cat() uses, so any streamable type works.
 *
 * @tparam Args Types of the format arguments
 * @param format_str Format string with "{}" placeholders
 * @param args Values to substitute, in order
 * @return The formatted string
 * @example
 * @code
 * auto msg = format("Check failed: {} - {}", cond_str, user_msg);
 * @endcode
 */
template <typename... Args>
std::string format(std::string_view format_str, const Args&... args);

/**
 * @brief Padding specification for hexadecimal formatting
 */
enum class hex_pad
{
    none = 1,
    pad2,
    pad3,
    pad4,
    pad5,
    pad6,
    pad7,
    pad8,
    pad9,
    pad10,
    pad11,
    pad12,
    pad13,
    pad14,
    pad15,
    pad16
};

/**
 * @brief Format an integer value as a hexadecimal string with optional zero-padding
 * @tparam Int The integer type to format
 * @param value The value to format
 * @param padding The padding specification (default: no padding)
 * @return Hexadecimal string representation of the value
 * @note This function prevents sign-extension by casting to unsigned types
 * @example
 * @code
 * auto hex1 = format_hex(255);                        // Returns "ff"
 * auto hex2 = format_hex(255, hex_pad::pad4);         // Returns "00ff"
 * auto hex3 = format_hex(0x1234, hex_pad::pad8);      // Returns "00001234"
 * @endcode
 */
template <typename Int>
std::string format_hex(Int value, hex_pad padding = hex_pad::none);

/**
 * @brief Concatenate multiple values into a single string
 * @tparam Args The types of arguments to concatenate
 * @param args The values to concatenate
 * @return Concatenated string
 * @note This function efficiently concatenates strings, string_views, and numeric types
 * @note Uses std::ostringstream for efficient multi-argument concatenation
 * @example
 * @code
 * auto str1 = str_cat("Hello", " ", "World");           // Returns "Hello World"
 * auto str2 = str_cat("Value: ", 42, ", Done");         // Returns "Value: 42, Done"
 * auto str3 = str_cat("Pi: ", 3.14159);                 // Returns "Pi: 3.14159"
 * @endcode
 */
template <typename... Args>
std::string str_cat(const Args&... args);

/**
 * @brief Append multiple values to an existing string
 * @tparam Args The types of arguments to append
 * @param result Pointer to the string to append to
 * @param args The values to append
 * @note This function efficiently appends strings, string_views, and numeric types
 * @note More efficient than repeated operator+= for multiple arguments
 * @example
 * @code
 * std::string s = "Start";
 * str_append(&s, " ", "Middle", " ", 123);  // s becomes "Start Middle 123"
 * @endcode
 */
template <typename... Args>
void str_append(std::string* result, const Args&... args);

/**
 * @brief Check if a string contains a specific character
 * @param haystack The string to search in
 * @param needle The character to search for
 * @return true if the character is found, false otherwise
 * @note This is a simple wrapper around std::string_view::find
 * @example
 * @code
 * bool has_colon = str_contains("hello:world", ':');  // Returns true
 * bool has_x = str_contains("hello", 'x');            // Returns false
 * @endcode
 */
inline bool str_contains(std::string_view haystack, char needle) noexcept
{
    return haystack.find(needle) != haystack.npos;
}

// =============================================================================
// TEMPLATE IMPLEMENTATIONS
// =============================================================================

namespace internal
{
// Helper to convert a value to string. Floating-point types are excluded here
// and handled by the dedicated overload below, which would otherwise be
// ambiguous against this one.
template <typename T>
inline std::enable_if_t<!std::is_floating_point_v<T>> to_string_helper(
    std::ostringstream& oss, const T& value)
{
    oss << value;
}

// Specialization for string_view to avoid unnecessary conversions
inline void to_string_helper(std::ostringstream& oss, std::string_view value)
{
    oss << value;
}

// Specialization for const char* to avoid unnecessary conversions
inline void to_string_helper(std::ostringstream& oss, const char* value)
{
    if (value != nullptr)
    {
        oss << value;
    }
}

// Overload for bool: operator<< would print 1/0, which is not what a log
// message wants (and not what the fmt-based formatter this replaced printed).
inline void to_string_helper(std::ostringstream& oss, bool value)
{
    oss << (value ? "true" : "false");
}

// Shortest decimal text that parses back to exactly this value, implemented in
// string_util.cpp so the platform differences stay in one translation unit.
LOGGING_API std::string shortest_round_trip(float value);
LOGGING_API std::string shortest_round_trip(double value);
LOGGING_API std::string shortest_round_trip(long double value);

// Overload for floating point: operator<<'s default precision is 6 significant
// digits, which silently truncates (3.14159265358979 -> "3.14159"), while
// max_digits10 over-prints (0.1 -> "0.10000000000000001"). Use the shortest
// round-tripping form, which is also what the fmt formatter this replaced
// produced.
template <typename T>
inline std::enable_if_t<std::is_floating_point_v<T>> to_string_helper(
    std::ostringstream& oss, T value)
{
    oss << shortest_round_trip(value);
}

// Convert a single value to its string form, for format()'s placeholders
template <typename T>
inline std::string to_string_one(const T& value)
{
    std::ostringstream oss;
    to_string_helper(oss, value);
    return oss.str();
}
}  // namespace internal

template <typename... Args>
std::string format(std::string_view format_str, const Args&... args)
{
    if constexpr (sizeof...(Args) == 0)
    {
        return vformat(format_str, nullptr, 0);
    }
    else
    {
        const std::string arg_strings[] = {internal::to_string_one(args)...};
        return vformat(format_str, arg_strings, sizeof...(Args));
    }
}

template <typename Int>
std::string format_hex(Int value, hex_pad padding)
{
    // Prevent sign-extension by casting to unsigned types
    static_assert(
        sizeof(value) == 1 || sizeof(value) == 2 || sizeof(value) == 4 || sizeof(value) == 8,
        "Unsupported integer type for hex formatting");

    uint64_t unsigned_value = sizeof(value) == 1   ? static_cast<uint8_t>(value)
                              : sizeof(value) == 2 ? static_cast<uint16_t>(value)
                              : sizeof(value) == 4 ? static_cast<uint32_t>(value)
                                                   : static_cast<uint64_t>(value);

    std::ostringstream oss;
    oss << std::hex;

    // Apply padding if specified
    int pad_width = static_cast<int>(padding);
    if (pad_width > 1)
    {
        oss << std::setfill('0') << std::setw(pad_width);
    }

    oss << unsigned_value;
    return oss.str();
}

template <typename... Args>
std::string str_cat(const Args&... args)
{
    std::ostringstream oss;
    (internal::to_string_helper(oss, args), ...);
    return oss.str();
}

template <typename... Args>
void str_append(std::string* result, const Args&... args)
{
    if (result == nullptr)
    {
        return;
    }

    std::ostringstream oss;
    (internal::to_string_helper(oss, args), ...);
    *result += oss.str();
}

LOGGING_FORCE_INLINE std::string to_lower(std::string_view input)
{
    std::string result(input);
    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return result;
}
}  // namespace strings
}  // namespace logging
