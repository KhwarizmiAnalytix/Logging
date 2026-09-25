// Implementation lives in glog_backend.h: GlogBackend must be a complete,
// nameable type in backend.h (included by factory.cpp and logger.cpp) so the
// backend is selected via a compile-time type alias, not a runtime vtable.
#include "src/backend/glog_backend.h"
