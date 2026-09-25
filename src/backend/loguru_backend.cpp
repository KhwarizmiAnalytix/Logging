// Implementation lives in loguru_backend.h: LoguruBackend must be a complete,
// nameable type in backend.h (included by factory.cpp and logger.cpp) so the
// backend is selected via a compile-time type alias, not a runtime vtable.
#include "src/backend/loguru_backend.h"
