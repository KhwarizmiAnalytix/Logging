#ifndef LOGGING_INCLUDE_BACKEND_BACKEND_H
#define LOGGING_INCLUDE_BACKEND_BACKEND_H

// The single remaining compile-time branch: pick the concrete backend type.
// ActiveBackend is a concrete class (no virtual base, no vtable) so every
// call through active_backend() is an ordinary, staticly-dispatched member
// function call -- ripe for inlining, unlike the old Backend-interface
// design this replaced.
#include "include/backend/backend_types.h"

#if LOGGING_HAS_LOGURU
#include "include/backend/loguru_backend.h"
#elif LOGGING_HAS_NATIVE
#include "include/backend/native_backend.h"
#elif LOGGING_HAS_SPDLOG
#include "include/backend/spdlog_backend.h"
#elif LOGGING_HAS_GLOG
#include "include/backend/glog_backend.h"
#else
#error "No logging backend selected"
#endif

namespace logging
{
namespace backend
{

#if LOGGING_HAS_LOGURU
using ActiveBackend = detail::LoguruBackend;
#elif LOGGING_HAS_NATIVE
using ActiveBackend = detail::NativeBackend;
#elif LOGGING_HAS_SPDLOG
using ActiveBackend = detail::SpdlogBackend;
#elif LOGGING_HAS_GLOG
using ActiveBackend = detail::GlogBackend;
#endif

// The process-wide singleton, constructed on first use. Concrete return type
// (not a base-class reference) means calls through it are static dispatch.
ActiveBackend& active_backend();

}  // namespace backend
}  // namespace logging
#endif  // LOGGING_INCLUDE_BACKEND_BACKEND_H
