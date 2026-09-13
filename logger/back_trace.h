#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common/logging_export.h"

namespace logging
{
/**
 * @brief Stack frame information for detailed backtrace analysis
 */
struct stack_frame
{
    std::string function_name;         ///< Demangled function name
    std::string object_file;           ///< Executable or library name
    std::string offset_into_function;  ///< Hexadecimal offset (e.g., "0x1a2b")
    void*       return_address;        ///< Return address pointer
    size_t      frame_number;          ///< Frame index in the stack
    std::string source_file;           ///< Source file (if available with debug symbols)
    int         source_line;           ///< Source line number (if available)

    stack_frame() : return_address(nullptr), frame_number(0), source_line(-1) {}
};

/**
 * @brief Configuration options for stack trace capture
 */
struct backtrace_options
{
    size_t frames_to_skip           = 0;      ///< Number of top frames to skip
    size_t maximum_number_of_frames = 64;     ///< Maximum frames to capture
    bool   skip_python_frames       = true;   ///< Skip Python interpreter frames
    bool   include_addresses        = true;   ///< Include memory addresses in output
    bool   include_offsets          = true;   ///< Include function offsets
    bool   compact_format           = false;  ///< Use compact single-line format
    bool   resolve_symbols          = true;   ///< Attempt symbol resolution (Windows)
    bool   include_source_info      = false;  ///< Include source file/line (requires debug symbols)
};

/**
 * @brief Enhanced stack trace capture and formatting utility
 *
 * Provides cross-platform stack trace capture with support for:
 * - Symbol demangling (C++ name demangling)
 * - Configurable output formatting
 * - Python frame filtering
 * - Windows symbol resolution (DbgHelp)
 * - Thread-safe operation
 *
 * **Platform Support**:
 * - Linux/Unix: Uses execinfo.h (backtrace/backtrace_symbols)
 * - Windows: Uses DbgHelp API (StackWalk64, SymFromAddr)
 * - macOS: Uses execinfo.h with platform-specific parsing
 *
 * **Performance Considerations**:
 * - Stack capture is relatively fast (microseconds)
 * - Symbol resolution can be slower (milliseconds)
 * - Consider caching traces for frequently-called paths
 *
 * **Thread Safety**: All methods are thread-safe
 */
namespace back_trace
{
/**
     * @brief Capture and format current stack trace as string
     *
     * @param frames_to_skip Number of top frames to skip (default: 0)
     * @param maximum_number_of_frames Maximum frames to capture (default: 64)
     * @param skip_python_frames Skip Python interpreter frames (default: true)
     * @return Formatted stack trace string
     *
     * **Example Output**:
     * ```
     * frame #0: memory::cpu::memory_allocator::allocate + 0x1a2b (0x7fff8a2b in libc.so)
     * frame #1: logging::logger::log + 0x45 (0x7fff8a45 in Core.dll)
     * frame #2: main + 0x12 (0x400512 in app.exe)
     * ```
     */
LOGGING_API std::string print(
    size_t frames_to_skip           = 0,
    size_t maximum_number_of_frames = 64,
    bool   skip_python_frames       = true);

/**
     * @brief Capture and format stack trace with custom options
     *
     * @param options Configuration for trace capture and formatting
     * @return Formatted stack trace string
     */
LOGGING_API std::string print(const backtrace_options& options);

/**
     * @brief Capture raw stack frames without formatting
     *
     * @param options Configuration for trace capture
     * @return Vector of stack frame information
     *
     * **Use Cases**:
     * - Custom formatting
     * - Programmatic analysis
     * - Caching for later formatting
     */
LOGGING_API std::vector<stack_frame> capture(
    const backtrace_options& options = backtrace_options());

/**
     * @brief Format captured stack frames to string
     *
     * @param frames Previously captured stack frames
     * @param options Formatting options
     * @return Formatted stack trace string
     */
LOGGING_API std::string format(
    const std::vector<stack_frame>& frames, const backtrace_options& options = backtrace_options());

/**
     * @brief Get compact single-line stack trace (for logging)
     *
     * @param max_frames Maximum number of frames (default: 5)
     * @return Compact trace like "main->foo->bar->baz"
     *
     * **Example**: `"main -> allocate_raw -> malloc -> __libc_start_main"`
     */
LOGGING_API std::string compact(size_t max_frames = 5);

/**
     * @brief Enable/disable automatic stack trace on errors
     *
     * @param enable 1 to enable, 0 to disable
     *
     * **Note**: Currently a no-op placeholder for future implementation
     */
LOGGING_API void set_stack_trace_on_error(int enable);

/**
     * @brief Whether exception construction should capture a stack trace
     */
LOGGING_API bool capture_on_error();

/**
     * @brief Check if stack trace capture is supported on this platform
     *
     * @return true if backtrace is available, false otherwise
     */
LOGGING_API bool is_supported();
};  // namespace back_trace
}  // namespace logging
