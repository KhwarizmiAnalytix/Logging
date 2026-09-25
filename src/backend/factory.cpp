#include "include/logging/backend.h"

#include <memory>

namespace logging
{
namespace backend
{

// The one remaining compile-time branch: pick the concrete backend. Everything
// else in the library calls through active_backend() with no #if in sight.
std::unique_ptr<Backend> create_backend()
{
#if LOGGING_HAS_LOGURU
    return create_loguru_backend();
#elif LOGGING_HAS_NATIVE
    return create_native_backend();
#elif LOGGING_HAS_SPDLOG
    return create_spdlog_backend();
#elif LOGGING_HAS_GLOG
    return create_glog_backend();
#else
#error "No logging backend selected"
#endif
}

Backend& active_backend()
{
    // Function-local static: constructed once, on first use, thread-safe under
    // C++11 magic statics. This is the "singleton instantiated at initialization"
    // that replaces per-call backend dispatch.
    static std::unique_ptr<Backend> instance = create_backend();
    return *instance;
}

}  // namespace backend
}  // namespace logging
