#include "include/logging/backend.h"

namespace logging
{
namespace backend
{

std::unique_ptr<Backend> create_backend()
{
#if LOGGING_HAS_NATIVE
    return create_native_backend();
#elif LOGGING_HAS_SPDLOG
    return create_spdlog_backend();
#elif LOGGING_HAS_LOGURU
    return create_loguru_backend();
#elif LOGGING_HAS_GLOG
    return create_glog_backend();
#else
    return nullptr;
#endif
}

}  // namespace backend
}  // namespace logging
