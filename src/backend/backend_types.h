#ifndef LOGGING_SRC_BACKEND_BACKEND_TYPES_H
#define LOGGING_SRC_BACKEND_BACKEND_TYPES_H

// Shared, backend-agnostic types. No virtual interface here: exactly one
// concrete backend type is compiled in (selected by backend.h's #if), so the
// core I/O path dispatches statically -- no vtable, no runtime branch.

namespace logging
{
namespace backend
{

/**
 * One-time initialization inputs handed to the selected backend. The facade
 * owns the signal-handler policy and thread name; the backend performs only
 * its own library setup (loguru::init, google::InitGoogleLogging, ...).
 */
struct init_options
{
    const char* verbosity_flag        = "-v";
    const char* main_thread_name      = nullptr;  // null when unset
    bool        unsafe_signal_handler = false;
    bool        sigabrt               = false;
    bool        sigbus                = false;
    bool        sigfpe                = false;
    bool        sigill                = false;
    bool        sigint                = false;
    bool        sigsegv               = false;
    bool        sigterm               = false;
};

}  // namespace backend
}  // namespace logging
#endif  // LOGGING_SRC_BACKEND_BACKEND_TYPES_H
