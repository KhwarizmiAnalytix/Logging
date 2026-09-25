#include "include/logging/backend.h"
#include "include/logger/logger.h"
#include "include/logger/logger_verbosity_enum.h"

#include <memory>
#include <mutex>

namespace logging {
namespace dispatcher {
namespace {

std::mutex g_backend_mutex;
std::unique_ptr<backend::Backend> g_backend;

backend::Backend* get_backend() {
    std::scoped_lock lock(g_backend_mutex);
    if (!g_backend) {
#if LOGGING_HAS_NATIVE
        g_backend = backend::create_native_backend();
#elif LOGGING_HAS_SPDLOG
        g_backend = backend::create_spdlog_backend();
#elif LOGGING_HAS_LOGURU
        g_backend = backend::create_loguru_backend();
#elif LOGGING_HAS_GLOG
        g_backend = backend::create_glog_backend();
#endif
    }
    return g_backend.get();
}

}  // namespace

// Public dispatcher functions that route through the backend

void log(logger_verbosity_enum verbosity, const char* fname, unsigned int lineno, const char* message) {
    auto* backend = get_backend();
    if (backend) {
        backend->log(static_cast<int>(verbosity), fname, lineno, message);
    }
}

void set_cutoff(logger_verbosity_enum level) {
    auto* backend = get_backend();
    if (backend) {
        backend->set_cutoff(static_cast<int>(level));
    }
}

logger_verbosity_enum get_cutoff() {
    auto* backend = get_backend();
    if (backend) {
        return static_cast<logger_verbosity_enum>(backend->get_cutoff());
    }
    return logger_verbosity_enum::VERBOSITY_INFO;
}

void log_to_file(const char* path, bool truncate, logger_verbosity_enum verbosity) {
    auto* backend = get_backend();
    if (backend) {
        backend->log_to_file(path, truncate, static_cast<int>(verbosity));
    }
}

void end_log_to_file(const char* path) {
    auto* backend = get_backend();
    if (backend) {
        backend->end_log_to_file(path);
    }
}

void set_console_mode(bool enabled) {
    auto* backend = get_backend();
    if (backend) {
        backend->set_console_mode(enabled);
    }
}

bool get_console_mode() {
    auto* backend = get_backend();
    if (backend) {
        return backend->get_console_mode();
    }
    return true;
}

void add_callback(const char* id,
    void (*log_handler)(void*, const logger::Message&),
    void* user_data,
    logger_verbosity_enum verbosity,
    void (*on_close)(void*),
    void (*on_flush)(void*)) {
    auto* backend = get_backend();
    if (backend) {
        backend->add_callback(id,
            reinterpret_cast<void*>(log_handler),
            user_data,
            static_cast<int>(verbosity),
            reinterpret_cast<void*>(on_close),
            reinterpret_cast<void*>(on_flush));
    }
}

bool remove_callback(const char* id) {
    auto* backend = get_backend();
    if (backend) {
        return backend->remove_callback(id);
    }
    return false;
}

void flush() {
    auto* backend = get_backend();
    if (backend) {
        backend->flush();
    }
}

void set_thread_name(const std::string& name) {
    auto* backend = get_backend();
    if (backend) {
        backend->set_thread_name(name);
    }
}

}  // namespace dispatcher
}  // namespace logging
