
#ifndef LOGGING_BACKEND_TRAITS_H
#define LOGGING_BACKEND_TRAITS_H

#include "include/logging/level.h"

namespace logging
{

/**
 * Backend capability traits - explicitly declare which features each backend supports.
 * Prevents silent failures when a feature is not implemented.
 */

namespace backend_traits
{

template <int BackendId> struct traits
{
    static constexpr bool supports_callbacks         = false;
    static constexpr bool supports_file_sink         = false;
    static constexpr bool supports_async             = false;
    static constexpr bool supports_structured_fields = false;
};

// Native backend - supports callbacks and file sink
#if LOGGING_HAS_NATIVE
template <> struct traits<1>
{  // NATIVE backend ID
    static constexpr bool supports_callbacks         = true;
    static constexpr bool supports_file_sink         = true;
    static constexpr bool supports_async             = false;
    static constexpr bool supports_structured_fields = false;
};
#endif

// Loguru backend - supports callbacks and file sink
#if LOGGING_HAS_LOGURU
template <> struct traits<2>
{  // LOGURU backend ID
    static constexpr bool supports_callbacks         = true;
    static constexpr bool supports_file_sink         = true;
    static constexpr bool supports_async             = false;
    static constexpr bool supports_structured_fields = false;
};
#endif

// spdlog backend - supports callbacks and file sink
#if LOGGING_HAS_SPDLOG
template <> struct traits<3>
{  // SPDLOG backend ID
    static constexpr bool supports_callbacks         = true;
    static constexpr bool supports_file_sink         = true;
    static constexpr bool supports_async             = true;
    static constexpr bool supports_structured_fields = false;
};
#endif

// glog backend - NO callback support (explicit limitation)
#if LOGGING_HAS_GLOG
template <> struct traits<4>
{                                                      // GLOG backend ID
    static constexpr bool supports_callbacks = false;  // glog doesn't support custom callbacks
    static constexpr bool supports_file_sink = true;
    static constexpr bool supports_async     = false;
    static constexpr bool supports_structured_fields = false;
};
#endif

// Determine which backend is currently active and validate at compile time
#if LOGGING_HAS_NATIVE
constexpr int ACTIVE_BACKEND = 1;
using active_traits          = traits<1>;
#elif LOGGING_HAS_LOGURU
constexpr int ACTIVE_BACKEND = 2;
using active_traits          = traits<2>;
#elif LOGGING_HAS_SPDLOG
constexpr int ACTIVE_BACKEND = 3;
using active_traits          = traits<3>;
#elif LOGGING_HAS_GLOG
constexpr int ACTIVE_BACKEND = 4;
using active_traits          = traits<4>;
#else
#error "No logging backend selected"
#endif

// Helper function to enforce callback support at compile time
template <typename T = void> constexpr void validate_callback_support()
{
    static_assert(active_traits::supports_callbacks,
        "The selected logging backend does not support callbacks. "
        "Use NATIVE, LOGURU, or SPDLOG backend instead of GLOG.");
}

}  // namespace backend_traits

}  // namespace logging
#endif  // LOGGING_BACKEND_TRAITS_H
