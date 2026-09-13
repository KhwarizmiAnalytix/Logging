/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
 */

#pragma once

#ifndef LOGGING_PORTABLE_MACROS_INCLUDED_
#define LOGGING_PORTABLE_MACROS_INCLUDED_

// ============================================================================
// Minimum compiler version (Logging targets C++17+)
// ============================================================================
#if defined(_MSC_VER) && _MSC_VER < 1910
#error LOGGING requires MSVC++ 15.0 (Visual Studio 2017) or newer for C++17 support
#endif

#if !defined(__clang__) && defined(__GNUC__) && \
    (__GNUC__ < 7 || (__GNUC__ == 7 && __GNUC_MINOR__ < 1))
#error LOGGING requires GCC 7.1 or newer for C++17 support
#endif

#if defined(__clang__) && (__clang_major__ < 5)
#error LOGGING requires Clang 5.0 or newer for C++17 support
#endif

// Export macros: common/logging_export.h (LOGGING_API, LOGGING_VISIBILITY, etc.).

// ============================================================================
// Forced inline
// ============================================================================
#if defined(_MSC_VER)
#define LOGGING_FORCE_INLINE __forceinline
#elif defined(__INTEL_COMPILER)
#define LOGGING_FORCE_INLINE inline __attribute__((always_inline))
#elif defined(__clang__)
#define LOGGING_FORCE_INLINE inline __attribute__((always_inline))
#elif defined(__GNUC__)
#define LOGGING_FORCE_INLINE inline __attribute__((always_inline))
#else
#define LOGGING_FORCE_INLINE inline
#endif

// ============================================================================
// [[unlikely]] / __builtin_expect (used by util/exception.h)
// ============================================================================
#if defined(__cplusplus) && defined(__has_cpp_attribute)
#define LOGGING_HAVE_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#define LOGGING_HAVE_CPP_ATTRIBUTE(x) 0
#endif

#if __cplusplus >= 202002L && LOGGING_HAVE_CPP_ATTRIBUTE(unlikely)
#define LOGGING_UNLIKELY(expr) (expr) [[unlikely]]
#elif defined(__GNUC__) || defined(__ICL) || defined(__clang__)
#define LOGGING_UNLIKELY(expr) (__builtin_expect(static_cast<bool>(expr), 0))
#else
#define LOGGING_UNLIKELY(expr) (expr)
#endif

// ============================================================================
// Suppress unused parameters / variables
// ============================================================================
#if __cplusplus >= 201703L
#define LOGGING_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
#define LOGGING_UNUSED __attribute__((unused))
#elif defined(_MSC_VER)
#define LOGGING_UNUSED __pragma(warning(suppress : 4100))
#else
#define LOGGING_UNUSED
#endif

// ============================================================================
// Build feature defaults
//
// CMake and Bazel define these explicitly for Logging targets. Keeping source
// defaults makes direct compilation of the public headers well-defined too.
// ============================================================================
#ifndef LOGGING_FORMAT_USE_STD
#define LOGGING_FORMAT_USE_STD 0
#endif

#ifndef LOGGING_HAS_MAGICENUM
#define LOGGING_HAS_MAGICENUM 0
#endif

#ifndef LOGGING_PORTABLE_FLOAT_FORMAT
#define LOGGING_PORTABLE_FLOAT_FORMAT 0
#endif

#ifndef LOGGING_DEFAULT_EXCEPTION_MODE_LOG_FATAL
#define LOGGING_DEFAULT_EXCEPTION_MODE_LOG_FATAL 0
#endif

// __cxa_demangle availability (util/string_util.cpp). CMake probes this
// capability and Bazel supplies a platform-specific definition. Retain the
// compiler-based fallback for consumers that compile the headers directly.
#ifndef LOGGING_HAS_CXA_DEMANGLE
#if defined(__ANDROID__) && (defined(__i386__) || defined(__x86_64__))
#define LOGGING_HAS_CXA_DEMANGLE 0
#elif defined(__clang__) && !defined(_MSC_VER)
#define LOGGING_HAS_CXA_DEMANGLE 1
#elif defined(__GNUC__)
#if (__GNUC__ >= 4 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)) && !defined(__mips__)
#define LOGGING_HAS_CXA_DEMANGLE 1
#else
#define LOGGING_HAS_CXA_DEMANGLE 0
#endif
#else
#define LOGGING_HAS_CXA_DEMANGLE 0
#endif
#endif

// ============================================================================
// Non-copyable / non-movable types (logger/logger.h)
// ============================================================================
#define LOGGING_DELETE_COPY_AND_MOVE(type)   \
private:                                     \
    type(const type&)              = delete; \
    type& operator=(const type& a) = delete; \
    type(type&&)                   = delete; \
    type& operator=(type&&)        = delete; \
                                             \
public:

#endif  // LOGGING_PORTABLE_MACROS_INCLUDED_
