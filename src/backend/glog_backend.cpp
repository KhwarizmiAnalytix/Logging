#include "include/logging/backend.h"

namespace logging {
namespace backend {

// Stub implementation - glog backend extraction deferred to Phase 2
std::unique_ptr<Backend> create_glog_backend() {
    return nullptr;
}

}  // namespace backend
}  // namespace logging
