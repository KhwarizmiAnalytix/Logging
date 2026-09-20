/*
 * XSigma Logging library — DLL export/import (same pattern as Parallel).
 */
#pragma once

#define LOGGING_VISIBILITY_ENUM

#if defined(LOGGING_STATIC_DEFINE)
#define LOGGING_API
#define LOGGING_VISIBILITY
#define LOGGING_IMPORT
#define LOGGING_HIDDEN

#elif defined(LOGGING_SHARED_DEFINE)
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef LOGGING_BUILDING_DLL
#define LOGGING_API __declspec(dllexport)
#else
#define LOGGING_API __declspec(dllimport)
#endif
#define LOGGING_VISIBILITY
#define LOGGING_IMPORT __declspec(dllimport)
#define LOGGING_HIDDEN
#elif defined(__GNUC__) && __GNUC__ >= 4
#define LOGGING_API __attribute__((visibility("default")))
#define LOGGING_VISIBILITY __attribute__((visibility("default")))
#define LOGGING_IMPORT __attribute__((visibility("default")))
#define LOGGING_HIDDEN __attribute__((visibility("hidden")))
#else
#define LOGGING_API
#define LOGGING_VISIBILITY
#define LOGGING_IMPORT
#define LOGGING_HIDDEN
#endif

#else
#define LOGGING_API
#define LOGGING_VISIBILITY
#define LOGGING_IMPORT
#define LOGGING_HIDDEN
#endif
