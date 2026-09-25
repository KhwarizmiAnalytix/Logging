
#ifndef LOGGING_SRC_BACKEND_BACKEND_SHARED_H
#define LOGGING_SRC_BACKEND_BACKEND_SHARED_H

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

#include "include/logger/logger_verbosity_enum.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdio>
#include <windows.h>
#endif

namespace logging
{
namespace backend
{
namespace shared
{

// Trailing path component, without allocating.
inline const char* basename_from_path(const char* fname)
{
    if (fname == nullptr)
    {
        return "";
    }
    const char* filename = fname;
    for (const char* p = fname; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            filename = p + 1;
        }
    }
    return filename;
}

// Create the directories leading to `path`, ignoring errors (best effort).
inline void ensure_parent_directory(const char* path)
{
    if ((path == nullptr) || *path == '\0')
    {
        return;
    }
    const std::filesystem::path file_path(path);
    if (!file_path.has_parent_path())
    {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(file_path.parent_path(), error);
}

#if defined(_WIN32)
// Attach to (or allocate) a console so a GUI host still sees stderr output.
inline void ensure_windows_console()
{
    if (GetConsoleWindow() != nullptr)
    {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0)
    {
        if (AllocConsole() == 0)
        {
            return;
        }
        SetConsoleTitleW(L"XSigma logging");
    }
    FILE* stream = nullptr;
    (void)freopen_s(&stream, "CONOUT$", "w", stdout);
    (void)freopen_s(&stream, "CONOUT$", "w", stderr);
    (void)freopen_s(&stream, "CONIN$", "r", stdin);
}
#endif

inline void abort_if_fatal(logger_verbosity_enum severity)
{
    if (severity == logger_verbosity_enum::VERBOSITY_FATAL)
    {
        std::abort();
    }
}

// Copy `name` into a fixed thread-name buffer, always NUL-terminated.
inline void copy_thread_name(char* dest, std::size_t dest_size, const std::string& name)
{
    if ((dest == nullptr) || dest_size == 0)
    {
        return;
    }
    std::strncpy(dest, name.c_str(), dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// Thread-local reentrancy guard: prevents a user callback that logs from
// re-entering callback dispatch (which would deadlock or recurse). One flag
// per program (inline thread_local), shared across every backend TU.
inline thread_local bool g_in_user_callback = false;

class CallbackReentrancyGuard
{
public:
    CallbackReentrancyGuard() : was_in_callback_(g_in_user_callback) { g_in_user_callback = true; }
    ~CallbackReentrancyGuard() { g_in_user_callback = was_in_callback_; }

    CallbackReentrancyGuard(const CallbackReentrancyGuard&)            = delete;
    CallbackReentrancyGuard& operator=(const CallbackReentrancyGuard&) = delete;

    bool is_reentrant() const { return was_in_callback_; }

private:
    bool was_in_callback_;
};

/**
 * Blocks until `in_flight` drops to zero, then returns true, meaning it is
 * now safe for the caller to release resources tied to the entry (e.g. call
 * its close handler): no invocation of that entry's callback, started before
 * the entry was unpublished (removed from the lookup map/sink list) under
 * the same lock, is still running.
 *
 * Returns false WITHOUT waiting when called from inside a callback
 * invocation on this thread (g_in_user_callback true): spinning here would
 * deadlock a callback that removes itself, since this thread's own
 * increment can't be decremented until the callback (which is blocked here)
 * returns. The caller should still run its cleanup in that case, accepting a
 * narrow residual race against a *different* thread concurrently invoking
 * the same entry -- unavoidable without a reentrant-aware wait queue, and a
 * self-removing callback already implies the caller controls its own
 * lifetime on this thread.
 */
inline bool wait_for_callback_drain(const std::atomic<int>& in_flight)
{
    if (g_in_user_callback)
    {
        return false;
    }
    while (in_flight.load(std::memory_order_acquire) > 0)
    {
        std::this_thread::yield();
    }
    return true;
}

}  // namespace shared
}  // namespace backend
}  // namespace logging

#endif  // LOGGING_SRC_BACKEND_BACKEND_SHARED_H
