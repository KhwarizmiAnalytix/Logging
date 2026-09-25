// Implementation lives in native_backend.h: NativeBackend must be a complete,
// nameable type in backend.h (included by factory.cpp and logger.cpp) so the
// backend is selected via a compile-time type alias, not a runtime vtable.
#include "src/backend/native_backend.h"
