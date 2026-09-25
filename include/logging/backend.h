
#ifndef LOGGING_BACKEND_H
#define LOGGING_BACKEND_H

#include <memory>
#include <string>
#include <string_view>

namespace logging
{

// Forward declaration - avoid circular dependency
namespace logger_ns
{
struct Message;
}

namespace backend
{

class Backend
{
public:
    virtual ~Backend() = default;

    // Core logging (using int for level to avoid logging/level.h dependency)
    virtual void log(
        int verbosity, const char* fname, unsigned int lineno, const char* message) = 0;

    // Cutoff management
    virtual void set_cutoff(int verbosity) = 0;
    virtual int  get_cutoff() const        = 0;

    // File output
    virtual void log_to_file(const char* path, bool truncate, int verbosity) = 0;
    virtual void end_log_to_file(const char* path)                           = 0;

    // Console mode
    virtual void set_console_mode(bool enabled) = 0;
    virtual bool get_console_mode() const       = 0;

    // Callbacks (Message type will be available at implementation time)
    virtual void add_callback(const char* id,
        void*                             log_handler_ptr,  // Opaque pointer to avoid dependency
        void*                             user_data,
        int                               verbosity,
        void*                             on_close_ptr = nullptr,
        void*                             on_flush_ptr = nullptr)            = 0;
    virtual bool remove_callback(const char* id) = 0;

    // Flush and cleanup
    virtual void flush()    = 0;
    virtual void shutdown() = 0;

    // Thread naming
    virtual void set_thread_name(std::string_view name) = 0;
};

std::unique_ptr<Backend> create_native_backend();
std::unique_ptr<Backend> create_spdlog_backend();
std::unique_ptr<Backend> create_loguru_backend();
std::unique_ptr<Backend> create_glog_backend();

}  // namespace backend
}  // namespace logging
#endif  // LOGGING_BACKEND_H
