// Implementation lives in spdlog_backend.h: SpdlogBackend must be a complete,
// nameable type in backend.h (included by factory.cpp and logger.cpp) so the
// backend is selected via a compile-time type alias, not a runtime vtable.
#include "src/backend/spdlog_backend.h"
