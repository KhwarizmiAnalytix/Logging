#include "src/backend/backend.h"

namespace logging
{
namespace backend
{

ActiveBackend& active_backend()
{
    // Function-local static: constructed once, on first use, thread-safe
    // under C++11 magic statics.
    static ActiveBackend instance;
    return instance;
}

}  // namespace backend
}  // namespace logging
