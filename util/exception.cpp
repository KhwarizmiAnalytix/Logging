#include "util/exception.h"

#include <atomic>
#include <cstdlib>  // for getenv
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>

#include "back_trace.h"
#include "logger/logger.h"
#include "util/string_util.h"

namespace logging
{
// ============================================================================
// Exception Mode Configuration
// ============================================================================

namespace
{
// Global exception mode with atomic access for thread safety
std::atomic<exception_mode> g_exception_mode_{
#if LOGGING_DEFAULT_EXCEPTION_MODE_LOG_FATAL
    exception_mode::LOG_FATAL
#else
    exception_mode::THROW
#endif
};

// Initialization flag to ensure environment is read only once
std::atomic<bool> g_exception_mode_initialized_{false};
std::mutex        g_exception_mode_init_mutex_;

std::function<std::string(void)>* GetFetchStackTrace()  // NOLINT
{
    static std::function<std::string(void)> func = []()
    {
        // Skip 2 frames: this lambda and GetFetchStackTrace
        return logging::back_trace::print(/*frames_to_skip=*/2, /*maximum_number_of_frames=*/32);
    };
    return &func;
};
}  // namespace

// ============================================================================
// Exception Mode API Implementation
// ============================================================================

exception_mode get_exception_mode() noexcept
{
    // Lazy initialization from environment on first access
    if (!g_exception_mode_initialized_.load(std::memory_order_acquire))
    {
        init_exception_mode_from_env();
    }
    return g_exception_mode_.load(std::memory_order_relaxed);
}

void set_exception_mode(exception_mode mode) noexcept
{
    g_exception_mode_.store(mode, std::memory_order_release);
    g_exception_mode_initialized_.store(true, std::memory_order_release);
}

void init_exception_mode_from_env() noexcept
{
    std::scoped_lock const lock(g_exception_mode_init_mutex_);

    // Double-check after acquiring lock
    if (g_exception_mode_initialized_.load(std::memory_order_acquire))
    {
        return;
    }

    const char* env_mode = std::getenv("LOGGING_EXCEPTION_MODE");
    if (env_mode != nullptr)
    {
        std::string mode_str(env_mode);
        if (mode_str == "LOG_FATAL" || mode_str == "log_fatal")
        {
            g_exception_mode_.store(exception_mode::LOG_FATAL, std::memory_order_relaxed);
            LOGGING_LOG_INFO("Exception mode set to LOG_FATAL from environment");
        }
        else if (mode_str == "THROW" || mode_str == "throw")
        {
            g_exception_mode_.store(exception_mode::THROW, std::memory_order_relaxed);
            LOGGING_LOG_INFO("Exception mode set to THROW from environment");
        }
        else
        {
            LOGGING_LOG_WARNING(
                "Invalid LOGGING_EXCEPTION_MODE value: {}. Using default.", mode_str);
        }
    }

    g_exception_mode_initialized_.store(true, std::memory_order_release);
}
// ============================================================================
// Exception Class Implementation
// ============================================================================

// Explicitly define the virtual destructor to ensure the vtable is generated
exception::~exception() = default;

//-----------------------------------------------------------------------------
exception::exception(
    std::string msg, std::string backtrace, const void* caller, exception_category category)
    : msg_(std::move(msg)), backtrace_(std::move(backtrace)), caller_(caller), category_(category)
{
    refresh_what();
}

//-----------------------------------------------------------------------------
exception::exception(source_location source_location, std::string msg, exception_category category)
    : exception(
          std::move(msg),
          std::string("Exception raised from ") + std::string(source_location.function) + " at " +
              std::string(source_location.file) + ":" + std::to_string(source_location.line) +
              " (most recent call first):\n" +
              (logging::back_trace::capture_on_error() ? (*GetFetchStackTrace())() : std::string{}),
          nullptr,
          category)
{
}

//-----------------------------------------------------------------------------
exception::exception(
    source_location            source_location,
    std::string                msg,
    std::shared_ptr<exception> nested,
    exception_category         category)
    : msg_(std::move(msg)),
      backtrace_(
          std::string("Exception raised from ") + std::string(source_location.function) + " at " +
          std::string(source_location.file) + ":" + std::to_string(source_location.line) +
          " (most recent call first):\n" +
          (logging::back_trace::capture_on_error() ? (*GetFetchStackTrace())() : std::string{})),
      caller_(nullptr),
      nested_exception_(std::move(nested)),  //NOLINT
      category_(category)
{
    refresh_what();
}

//-----------------------------------------------------------------------------
const char* exception::what() const noexcept
{
    return what_
        .ensure(
            [this]
            {
                try
                {
                    return compute_what(/*include_backtrace*/ true);
                }
                catch (...)
                {
                    // what() is noexcept, we need to return something here.
                    return std::string{"<Error computing exception::what()>"};
                }
            })
        .c_str();
}

//-----------------------------------------------------------------------------
std::string exception::compute_what(bool include_backtrace) const
{
    std::ostringstream oss;

    oss << msg();

    for (const auto& c : context())
    {
        oss << "\n  " << c;
    }

    if (include_backtrace)
    {
        oss << "\n" << backtrace();
    }

    // Include nested exception information
    if (nested_exception_)
    {
        oss << "\n\nCaused by:\n";
        oss << nested_exception_->what();
    }

    return oss.str();
}

//-----------------------------------------------------------------------------
void exception::refresh_what()
{
    what_.reset();
    what_without_backtrace_ = compute_what(/*include_backtrace*/ false);
}

//-----------------------------------------------------------------------------
void exception::add_context(std::string msg)
{
    context_.push_back(std::move(msg));
    // TODO: Calling add_context O(n) times has O(n^2) cost.  We can fix
    // this perf problem by populating the fields lazily... if this ever
    // actually is a problem.
    // NB: If you do fix this, make sure you do it in a thread safe way!
    // what() is almost certainly expected to be thread safe even when
    // accessed across multiple threads
    refresh_what();
}

//-----------------------------------------------------------------------------
namespace details
{
void check_fail(const char* func, const char* file, int line, const std::string& msg)
{
    logging::source_location loc{func, file, line};
    if (logging::get_exception_mode() == logging::exception_mode::THROW)
    {
        throw logging::exception(loc, msg, logging::exception_category::GENERIC);
    }
    LOGGING_LOG_FATAL("Check failed: {}", msg);
    std::abort();
}
}  // namespace details
}  // namespace logging
