#pragma once

#include <string>

#include "include/logger/logger_verbosity_enum.h"

namespace logging {

namespace logger {
struct Message;
}

namespace dispatcher {

// Core logging dispatcher functions
void log(logger_verbosity_enum verbosity, const char* fname, unsigned int lineno, const char* message);
void set_cutoff(logger_verbosity_enum level);
logger_verbosity_enum get_cutoff();
void log_to_file(const char* path, bool truncate, logger_verbosity_enum verbosity);
void end_log_to_file(const char* path);
void set_console_mode(bool enabled);
bool get_console_mode();

void add_callback(const char* id,
    void (*log_handler)(void*, const logger::Message&),
    void* user_data,
    logger_verbosity_enum verbosity,
    void (*on_close)(void*) = nullptr,
    void (*on_flush)(void*) = nullptr);

bool remove_callback(const char* id);
void flush();
void set_thread_name(const std::string& name);

}  // namespace dispatcher
}  // namespace logging
