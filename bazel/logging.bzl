"""Shared compile and link settings for Logging's Bazel targets."""

# =============================================================================
# Logging — shared Bazel compile/link helpers
# =============================================================================

def logging_copts(cstdlib_include = True):
    """Returns common compiler options for Logging targets.

    Args:
        cstdlib_include: Whether to force-include <cstdlib> on non-Windows.
    """
    return select({
        "//bazel:cxx17_windows": ["/std:c++17"],
        "//bazel:cxx23_windows": ["/std:c++latest"],
        "//bazel:cxx17": ["-std=c++17"],
        "//bazel:cxx23": ["-std=c++23"],
        "@platforms//os:windows": ["/std:c++20"],
        "//conditions:default": ["-std=c++20"],
    }) + select({
        "@platforms//os:windows": [
            "/Zc:__cplusplus",
            "/EHsc",
            "/bigobj",
            "/utf-8",
            "/wd4244",
            "/wd4267",
            "/wd4715",
            "/wd4018",
            "/WX",
        ],
        "//conditions:default": [
            "-Wall",
            "-Wextra",
            "-Wpedantic",
        ] + (["-include", "cstdlib"] if cstdlib_include else []),
    })

def _logging_platform_defines():
    """Returns project-wide preprocessor defines (Windows CRT safety)."""
    return select({
        "@platforms//os:windows": [
            "_CRT_SECURE_NO_DEPRECATE",
            "_CRT_NONSTDC_NO_DEPRECATE",
            "_CRT_SECURE_NO_WARNINGS",
            "_SCL_SECURE_NO_DEPRECATE",
            "_SCL_SECURE_NO_WARNINGS",
        ],
        "//conditions:default": [],
    })

def logging_linkopts():
    """Returns common linker options for Logging targets."""
    return select({
        "@platforms//os:windows": [],
        "@platforms//os:macos": [],
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    })

def logging_defines():
    """Returns compile definitions for Logging.

    Mirrors CMakeLists.txt: backend and feature flags.

    Returns:
        The selected backend, platform, and feature definitions.
    """
    defines = _logging_platform_defines()

    # Logging backend — mutually exclusive; default LOGURU (matches CMake LOGGING_BACKEND default)
    defines += select({
        "//bazel:logging_glog": [
            "LOGGING_HAS_LOGURU=0",
            "LOGGING_HAS_GLOG=1",
            "LOGGING_HAS_NATIVE=0",
            "LOGGING_HAS_SPDLOG=0",
        ],
        "//bazel:logging_native": [
            "LOGGING_HAS_LOGURU=0",
            "LOGGING_HAS_GLOG=0",
            "LOGGING_HAS_NATIVE=1",
            "LOGGING_HAS_SPDLOG=0",
        ],
        "//bazel:logging_loguru": [
            "LOGGING_HAS_LOGURU=1",
            "LOGGING_HAS_GLOG=0",
            "LOGGING_HAS_NATIVE=0",
            "LOGGING_HAS_SPDLOG=0",
        ],
        "//bazel:logging_spdlog": [
            "LOGGING_HAS_LOGURU=0",
            "LOGGING_HAS_GLOG=0",
            "LOGGING_HAS_NATIVE=0",
            "LOGGING_HAS_SPDLOG=1",
            "SPDLOG_FMT_EXTERNAL=1",
        ],
        "//conditions:default": [
            # LOGURU when no --define=logging_backend (matches CMake default)
            "LOGGING_HAS_LOGURU=1",
            "LOGGING_HAS_GLOG=0",
            "LOGGING_HAS_NATIVE=0",
            "LOGGING_HAS_SPDLOG=0",
        ],
    })

    # CMake probes <cxxabi.h>; Bazel's Windows toolchain does not provide it.
    defines += select({
        "//bazel:disable_logging_cxa_demangle": ["LOGGING_HAS_CXA_DEMANGLE=0"],
        "@platforms//os:windows": ["LOGGING_HAS_CXA_DEMANGLE=0"],
        "//conditions:default": ["LOGGING_HAS_CXA_DEMANGLE=1"],
    })
    defines += select({
        "//bazel:logging_portable_float_format": ["LOGGING_PORTABLE_FLOAT_FORMAT=1"],
        "//conditions:default": ["LOGGING_PORTABLE_FLOAT_FORMAT=0"],
    })
    defines += select({
        "//bazel:logging_std_format": ["LOGGING_FORMAT_USE_STD=1"],
        "//conditions:default": ["LOGGING_FORMAT_USE_STD=0"],
    })
    defines += select({
        "//bazel:logging_default_log_fatal": ["LOGGING_DEFAULT_EXCEPTION_MODE_LOG_FATAL=1"],
        "//conditions:default": ["LOGGING_DEFAULT_EXCEPTION_MODE_LOG_FATAL=0"],
    })

    return defines
