
#ifndef LOGGING_LOGGER_CONFIG_H
#define LOGGING_LOGGER_CONFIG_H

#include "logger_verbosity_enum.h"

namespace logging {

/**
 * Logging configuration object - replaces individual static booleans.
 *
 * Provides structured, type-safe configuration for logger initialization.
 * All settings have sensible defaults suitable for library use (conservative).
 *
 * Usage:
 *   logging::config cfg{
 *       .level = logging::logger_verbosity_enum::VERBOSITY_INFO,
 *       .console = true,
 *       .signals = {.enabled = false, .sigsegv = false, ...}
 *   };
 *   logging::logger::init(cfg);
 */

struct signal_config {
    bool enabled = false;          // Master switch (default: off, safer for libraries)
    bool sigabrt = false;          // SIGABRT handler
    bool sigbus = false;           // SIGBUS handler  (platform specific)
    bool sigfpe = false;           // SIGFPE handler  (arithmetic errors)
    bool sigill = false;           // SIGILL handler  (illegal instruction)
    bool sigint = false;           // SIGINT handler  (Ctrl+C)
    bool sigsegv = false;          // SIGSEGV handler (segmentation fault)
    bool sigterm = false;          // SIGTERM handler (termination signal)
};

struct config {
    /**
     * Base log level - messages more severe than this are always logged.
     * Default: VERBOSITY_INFO (conservative, doesn't spam with debug/trace).
     */
    logger_verbosity_enum level = logger_verbosity_enum::VERBOSITY_INFO;

    /**
     * Enable console (stderr) output.
     * Default: true (sensible for interactive use).
     */
    bool console = true;

    /**
     * Signal handler configuration.
     * Default: all disabled (appropriate for libraries that don't own process policy).
     */
    signal_config signals;

    /**
     * Thread name for this thread (optional).
     * Default: empty (no explicit naming).
     */
    const char* thread_name = nullptr;
};

}  // namespace logging
#endif  // LOGGING_LOGGER_CONFIG_H
