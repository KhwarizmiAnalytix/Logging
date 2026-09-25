#include "include/logging/backend.h"

namespace logging {
namespace backend {

// Stub implementation - loguru backend extraction deferred to Phase 2
std::unique_ptr<Backend> create_loguru_backend() {
    return nullptr;
}

}  // namespace backend
}  // namespace logging
