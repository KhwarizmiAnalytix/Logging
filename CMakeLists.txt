# =============================================================================
# Logging —
# CMakeLists.txt
#
# Structured log levels, back-trace capture, and pluggable backend support
# (Loguru default, spdlog, GLog, Native).
#
# Public compile definitions: LOGGING_HAS_SPDLOG=1/0 — spdlog backend active
# LOGGING_HAS_LOGURU=1/0 — Loguru backend active LOGGING_HAS_GLOG=1/0 —
# GLog backend active LOGGING_HAS_NATIVE=1/0 — Native (fmt-based) backend active
# =============================================================================
cmake_minimum_required(VERSION 3.16)

# Standalone CMake package — any C++ project can consume it via add_subdirectory;
# XSigma is one consumer, not a required host.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(
    Logging
    VERSION 1.0.0
    DESCRIPTION "Structured logging with pluggable backends (Loguru, spdlog, GLog, Native)"
    HOMEPAGE_URL "https://github.com/KhwarizmiAnalytix/Logging"
    LANGUAGES CXX C
  )
  set(LOGGING_STANDALONE ON)
  include(CTest)
  enable_testing()
  option(BUILD_SHARED_LIBS "Build shared libraries" ON)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
else()
  set(LOGGING_STANDALONE OFF)
endif()

# Own CMake modules: toolchain helpers (lto/cache/sanitize/...), the
# add_third_party_library machinery (third_party_helpers.cmake), and test
# dependency resolvers (LoggingDependencies.cmake) all live in Cmake/.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Cmake")

# add_third_party_library + alias/folder helpers. When embedded in XSigma these
# are already defined by XSigma's ThirdParty/CMakeLists.txt; including the
# repo's own copy is a harmless identical redefinition.
include(third_party_helpers)

# Standard, platform-correct install destinations (lib/lib64/etc. via
# CMAKE_INSTALL_LIBDIR) instead of hardcoding "lib"/"bin". Included early:
# CMAKE_INSTALL_INCLUDEDIR is referenced by target_include_directories()
# below, not just by the install() calls at the bottom of this file.
include(GNUInstallDirs)

# --- Section 1: Feature flags (LOGGING_ENABLE_*) and backend selector ---
include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
  "
  #include <cxxabi.h>
  #include <cstdlib>
  int main() {
    int status = 0;
    char* result = abi::__cxa_demangle(\"_Z1fv\", nullptr, nullptr, &status);
    std::free(result);
    return status;
  }
  "
  LOGGING_HAS_CXA_DEMANGLE_AVAILABLE
)
mark_as_advanced(LOGGING_HAS_CXA_DEMANGLE_AVAILABLE)

set(LOGGING_BACKEND "LOGURU"
    CACHE STRING "Logging backend to use. Options are NATIVE, LOGURU, GLOG, or SPDLOG"
)
set_property(CACHE LOGGING_BACKEND PROPERTY STRINGS NATIVE LOGURU GLOG SPDLOG)

# Link-Time Optimization — disabled when LOGGING_ENABLE_COVERAGE or LOGGING_ENABLE_SANITIZER is ON
# (LTO conflicts with instrumentation / sanitizers).
include(lto)
xsigma_lto_compute_default(_lto_default PROFILE SUPPORT)
set(LOGGING_LTO_MODE "${_lto_default}" CACHE STRING
                                             "LTO mode for Logging: off | thin | full | ipo | auto"
)
set_property(CACHE LOGGING_LTO_MODE PROPERTY STRINGS off thin full ipo auto)

# Coverage instrumentation — activates -fprofile-instr-generate / --coverage compiler flags for
# this module only. LTO is automatically skipped when coverage is enabled.
option(LOGGING_ENABLE_COVERAGE "Enable coverage instrumentation for Logging" OFF)

# Testing — gates add_subdirectory(Testing/Cxx).
option(LOGGING_ENABLE_TESTING "Build Logging test suite" ON)

# Examples — gates add_subdirectory(Examples) when Logging examples exist.
option(LOGGING_ENABLE_EXAMPLES "Build Logging example programs" OFF)

# Google Test integration for Logging tests.
option(LOGGING_ENABLE_GTEST "Enable GoogleTest for Logging" ON)

# Google Benchmark integration for Logging micro-benchmarks.
option(LOGGING_ENABLE_BENCHMARK "Enable Google Benchmark for Logging" ON)

# magic_enum — enum ↔ string helpers in util/string_util.h
option(LOGGING_ENABLE_MAGICENUM "Enable magic_enum static reflection for Logging" ON)

option(LOGGING_ENABLE_CXA_DEMANGLE
       "Enable Itanium ABI symbol demangling when the toolchain supports it"
       ${LOGGING_HAS_CXA_DEMANGLE_AVAILABLE}
)
option(LOGGING_ENABLE_PORTABLE_FLOAT_FORMAT
       "Use the portable floating-point formatter instead of std::to_chars" OFF
)
option(LOGGING_FORMAT_USE_STD "Use C++20 std::format instead of fmt" OFF)
set(LOGGING_DEFAULT_EXCEPTION_MODE "THROW"
    CACHE STRING "Default exception mode for Logging: THROW or LOG_FATAL"
)
set_property(CACHE LOGGING_DEFAULT_EXCEPTION_MODE PROPERTY STRINGS THROW LOG_FATAL)
if(NOT LOGGING_DEFAULT_EXCEPTION_MODE MATCHES "^(THROW|LOG_FATAL)$")
  message(FATAL_ERROR "LOGGING_DEFAULT_EXCEPTION_MODE must be THROW or LOG_FATAL")
endif()
if(LOGGING_ENABLE_CXA_DEMANGLE AND NOT LOGGING_HAS_CXA_DEMANGLE_AVAILABLE)
  message(FATAL_ERROR "LOGGING_ENABLE_CXA_DEMANGLE requires a toolchain with <cxxabi.h>")
endif()

# Icecream distributed compilation launcher (rarely needed; hidden by default).
option(LOGGING_ENABLE_ICECC "Use Icecream (icecc) distributed compiler for Logging" OFF)
mark_as_advanced(LOGGING_ENABLE_ICECC)

# Per-target compiler cache — wraps every compile command with ccache / sccache etc. to speed up
# incremental and CI rebuilds. ON by default.
option(LOGGING_ENABLE_CACHE "Enable compiler caching for the Logging target" ON)
mark_as_advanced(LOGGING_ENABLE_CACHE)

option(LOGGING_ENABLE_CLANGTIDY "Enable clang-tidy static analysis for Logging" OFF)
mark_as_advanced(LOGGING_ENABLE_CLANGTIDY)
option(LOGGING_ENABLE_FIX "Enable clang-tidy fix-errors and fix options for Logging" OFF)
mark_as_advanced(LOGGING_ENABLE_FIX)
option(LOGGING_ENABLE_IWYU "Enable include-what-you-use analysis for Logging" OFF)
mark_as_advanced(LOGGING_ENABLE_IWYU)
option(LOGGING_ENABLE_SANITIZER "Build Logging with sanitizer support (Clang/GCC only)" OFF)
set(LOGGING_SANITIZER_TYPE "address"
    CACHE STRING "Sanitizer type for Logging. Options: address, undefined, thread, memory, leak"
)
set_property(CACHE LOGGING_SANITIZER_TYPE PROPERTY STRINGS address undefined thread memory leak)
mark_as_advanced(LOGGING_ENABLE_SANITIZER LOGGING_SANITIZER_TYPE)
option(LOGGING_ENABLE_SPELL "Enable check-only spell checking for Logging" OFF)
mark_as_advanced(LOGGING_ENABLE_SPELL)
option(LOGGING_ENABLE_VALGRIND "Execute Logging test suite with Valgrind" OFF)
mark_as_advanced(LOGGING_ENABLE_VALGRIND)
set(LOGGING_LINKER_CHOICE "default" CACHE STRING
                                          "Linker for Logging: default, lld, mold, gold, lld-link"
)
set_property(CACHE LOGGING_LINKER_CHOICE PROPERTY STRINGS default lld mold gold lld-link)
mark_as_advanced(LOGGING_LINKER_CHOICE)
set(LOGGING_CACHE_BACKEND "none"
    CACHE STRING "Compiler cache backend for Logging. Options: none, ccache, sccache, buildcache"
)
set_property(CACHE LOGGING_CACHE_BACKEND PROPERTY STRINGS none ccache sccache buildcache)
mark_as_advanced(LOGGING_CACHE_BACKEND)

# C++ standard — applied to every TU in this module via CMAKE_CXX_STANDARD.
set(LOGGING_CXX_STANDARD "20" CACHE STRING "C++ standard for Logging. Options: 11 14 17 20 23")
set_property(CACHE LOGGING_CXX_STANDARD PROPERTY STRINGS 11 14 17 20 23)
if(NOT LOGGING_CXX_STANDARD MATCHES "^(11|14|17|20|23)$")
  message(FATAL_ERROR "LOGGING_CXX_STANDARD must be one of: 11 14 17 20 23")
endif()
if(LOGGING_FORMAT_USE_STD AND LOGGING_CXX_STANDARD LESS 20)
  message(FATAL_ERROR "LOGGING_FORMAT_USE_STD requires LOGGING_CXX_STANDARD to be at least 20")
endif()
if(LOGGING_FORMAT_USE_STD)
  set(CMAKE_CXX_STANDARD ${LOGGING_CXX_STANDARD})
  check_cxx_source_compiles(
    "
    #include <format>
    #include <string>
    int main() {
      return std::vformat(\"{}\", std::make_format_args(1)) == \"1\" ? 0 : 1;
    }
    "
    LOGGING_HAS_STD_FORMAT_AVAILABLE
  )
  mark_as_advanced(LOGGING_HAS_STD_FORMAT_AVAILABLE)
  if(NOT LOGGING_HAS_STD_FORMAT_AVAILABLE)
    message(FATAL_ERROR "LOGGING_FORMAT_USE_STD requires a standard library with std::format")
  endif()
endif()

# --- Section 2: LOGGING_ENABLE_* → LOGGING_HAS_* compile definitions --- Exactly one HAS_* will be
# 1; the others 0.

if(LOGGING_BACKEND STREQUAL "LOGURU")
  # Loguru — header-only, supports custom verbosity levels and callbacks.
  list(APPEND LOGGING_COMPILE_DEFINITIONS LOGGING_HAS_LOGURU=1 LOGGING_HAS_GLOG=0
       LOGGING_HAS_NATIVE=0 LOGGING_HAS_SPDLOG=0
  )
elseif(LOGGING_BACKEND STREQUAL "GLOG")
  # GLog — Google's production logging library; thread-safe sink management.
  list(APPEND LOGGING_COMPILE_DEFINITIONS LOGGING_HAS_LOGURU=0 LOGGING_HAS_GLOG=1
       LOGGING_HAS_NATIVE=0 LOGGING_HAS_SPDLOG=0
  )
elseif(LOGGING_BACKEND STREQUAL "NATIVE")
  # Native — fmt-based minimal backend with no external dependencies.
  list(APPEND LOGGING_COMPILE_DEFINITIONS LOGGING_HAS_LOGURU=0 LOGGING_HAS_GLOG=0
       LOGGING_HAS_NATIVE=1 LOGGING_HAS_SPDLOG=0
  )
elseif(LOGGING_BACKEND STREQUAL "SPDLOG")
  # spdlog — header-only, fast, feature-rich backend with colored output and async support.
  list(
    APPEND
    LOGGING_COMPILE_DEFINITIONS
    LOGGING_HAS_LOGURU=0
    LOGGING_HAS_GLOG=0
    LOGGING_HAS_NATIVE=0
    LOGGING_HAS_SPDLOG=1
    SPDLOG_FMT_EXTERNAL=1
  )
else()
  message(
    FATAL_ERROR
      "LOGGING_BACKEND must be one of: NATIVE, LOGURU, GLOG, SPDLOG (got '${LOGGING_BACKEND}')"
  )
endif()
list(APPEND LOGGING_DEPENDENCY_COMPILE_DEFINITIONS ${LOGGING_COMPILE_DEFINITIONS})
if(LOGGING_ENABLE_MAGICENUM)
  list(APPEND LOGGING_DEPENDENCY_COMPILE_DEFINITIONS LOGGING_HAS_MAGICENUM=1)
else()
  list(APPEND LOGGING_DEPENDENCY_COMPILE_DEFINITIONS LOGGING_HAS_MAGICENUM=0)
endif()
if(LOGGING_ENABLE_CXA_DEMANGLE)
  list(APPEND LOGGING_DEPENDENCY_COMPILE_DEFINITIONS LOGGING_HAS_CXA_DEMANGLE=1)
else()
  list(APPEND LOGGING_DEPENDENCY_COMPILE_DEFINITIONS LOGGING_HAS_CXA_DEMANGLE=0)
endif()
if(LOGGING_ENABLE_PORTABLE_FLOAT_FORMAT)
  list(APPEND LOGGING_DEPENDENCY_COMPILE_DEFINITIONS LOGGING_PORTABLE_FLOAT_FORMAT=1)
else()
  list(APPEND LOGGING_DEPENDENCY_COMPILE_DEFINITIONS LOGGING_PORTABLE_FLOAT_FORMAT=0)
endif()
if(LOGGING_FORMAT_USE_STD)
  list(APPEND LOGGING_DEPENDENCY_COMPILE_DEFINITIONS LOGGING_FORMAT_USE_STD=1)
else()
  list(APPEND LOGGING_DEPENDENCY_COMPILE_DEFINITIONS LOGGING_FORMAT_USE_STD=0)
endif()
if(LOGGING_DEFAULT_EXCEPTION_MODE STREQUAL "LOG_FATAL")
  list(APPEND LOGGING_DEPENDENCY_COMPILE_DEFINITIONS LOGGING_DEFAULT_EXCEPTION_MODE_LOG_FATAL=1)
else()
  list(APPEND LOGGING_DEPENDENCY_COMPILE_DEFINITIONS LOGGING_DEFAULT_EXCEPTION_MODE_LOG_FATAL=0)
endif()

# --- Section 3: CMake module includes --- Order: introspection → toolchain helpers →
# coverage/sanitizer → LTO/linker.

# a) Standard CMake introspection
include(CheckCXXSourceRuns)
include(CheckSymbolExists)

# b) Shared toolchain helpers
include(compiler_checks) # Detects compiler capabilities; sets PROJECT_* flags
include(checks) # Additional feature-check macros
include(build_type) # Validates / normalises CMAKE_BUILD_TYPE
include(platform) # Platform flags (macOS rpath, Windows /utf-8, …)

include(cache) # Configures per-target compiler cache launcher
include(CMakeDependentOption)
include(helper_macros)
if(LOGGING_ENABLE_SANITIZER)
  include(sanitize)
endif()
if(LOGGING_ENABLE_SPELL)
  include(spell)
endif()
if(LOGGING_ENABLE_VALGRIND)
  include(valgrind)
endif()
# c) Coverage gate — appends -fprofile-instr-generate / --coverage; LTO already cleared above.
if(LOGGING_ENABLE_COVERAGE)
  include(coverage)
elseif(MSVC)
  string(APPEND CMAKE_EXE_LINKER_FLAGS " /DEBUG:FULL")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " /DEBUG:FULL")
endif()

# --- Third-party dependencies owned by Logging ---
# Host (XSigma) ThirdParty vs this repo's nested ThirdParty/.
# glog / loguru / spdlog prefer the nested copies so a host can drop those
# duplicate submodules. fmt / magic_enum prefer the host copies when present
# so Core shares one fmt tree. Standalone (CMAKE_SOURCE_DIR == this repo)
# resolves both to the same nested ThirdParty/.
set(_logging_own_tp "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")
set(_logging_host_tp "${CMAKE_SOURCE_DIR}/ThirdParty")

macro(_logging_tp_root _component _out)
  set(_logging_prefer_nested FALSE)
  if("${_component}" STREQUAL "glog"
     OR "${_component}" STREQUAL "loguru"
     OR "${_component}" STREQUAL "spdlog")
    set(_logging_prefer_nested TRUE)
  endif()
  set(${_out} "")
  if(_logging_prefer_nested AND EXISTS "${_logging_own_tp}/${_component}/CMakeLists.txt")
    set(${_out} "${_logging_own_tp}")
  elseif(EXISTS "${_logging_host_tp}/${_component}/CMakeLists.txt")
    set(${_out} "${_logging_host_tp}")
  elseif(EXISTS "${_logging_own_tp}/${_component}/CMakeLists.txt")
    set(${_out} "${_logging_own_tp}")
  else()
    message(
      FATAL_ERROR
      "Logging cannot find ThirdParty/${_component} under "
      "${_logging_own_tp} or ${_logging_host_tp}"
    )
  endif()
endmacro()

_logging_tp_root(fmt _tp_fmt)
_logging_tp_root(magic_enum _tp_magic_enum)
_logging_tp_root(loguru _tp_loguru)
_logging_tp_root(glog _tp_glog)
_logging_tp_root(spdlog _tp_spdlog)

set(BUILD_TESTING OFF CACHE BOOL "Disable testing for third-party libraries" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable examples for third-party libraries" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "Disable benchmarks for third-party libraries" FORCE)

# loguru-specific options
set(LOGURU_BUILD_EXAMPLES OFF CACHE BOOL "Disable loguru examples" FORCE)
set(LOGURU_BUILD_TESTS OFF CACHE BOOL "Disable loguru tests" FORCE)
# set(LOGURU_USE_FMTLIB ON CACHE BOOL "Enable fmtlib formatting in loguru" FORCE)
set(LOGURU_WITH_STREAMS ON CACHE BOOL "Enable streams in loguru" FORCE)

# glog-specific options
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable glog examples" FORCE)
set(WITH_GFLAGS OFF CACHE BOOL "Disable gflags dependency for glog" FORCE)
set(WITH_GTEST OFF CACHE BOOL "Disable gtest for glog" FORCE)
set(WITH_GMOCK OFF CACHE BOOL "Disable gmock for glog" FORCE)

# spdlog-specific options (used if LIBRARY_TYPE is not HEADER_ONLY)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable spdlog tests" FORCE)
set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "Disable spdlog header-only tests" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Disable spdlog examples" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "Disable spdlog benchmarks" FORCE)

# fmt — Logging uses fmt directly and is processed before Core, so we initialise fmt here. Core's
# fmt is also brought in by Logging; when building the full tree this
# identical add_third_party_library(fmt...) call is a no-op (processed flag).
# XSIGMA_TARGETS is the keyword of the vendored ThirdParty helper — do not rename.
set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
set(FMT_DOC OFF CACHE BOOL "Disable fmt documentation" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install" FORCE)
set(FMT_HEADER_ONLY ON CACHE BOOL "Use header-only fmt library" FORCE)

add_third_party_library(
  fmt
  LIBRARY_TYPE
  STATIC
  SOURCE_DIR
  "${_tp_fmt}/fmt"
  ALIASES
  "fmt::fmt-header-only"
  XSIGMA_TARGETS
  "Fmt::fmt=fmt::fmt-header-only|fmt::fmt|fmt"
  INCLUDE_DIR
  "${_tp_fmt}/fmt/include"
  AUTO_ADD_TO_DEPENDENCIES
  OFF
)

# fmt is used by Logging source files directly; Fmt::fmt is not available here (it is created by
# Core, which is processed after Logging).
list(APPEND LOGGING_DEPENDENCY_INCLUDE_DIRS "${_tp_fmt}/fmt/include")
if(LOGGING_ENABLE_MAGICENUM)
  set(MAGIC_ENUM_OPT_BUILD_EXAMPLES OFF CACHE BOOL "Disable magic_enum examples" FORCE)
  set(MAGIC_ENUM_OPT_BUILD_TESTS OFF CACHE BOOL "Disable magic_enum tests" FORCE)
  add_third_party_library(
    magic_enum
    CONDITION
    LOGGING_ENABLE_MAGICENUM
    LIBRARY_TYPE
    HEADER_ONLY
    SOURCE_DIR
    "${_tp_magic_enum}/magic_enum"
    XSIGMA_TARGETS
    "MagicEnum::magic_enum=magic_enum"
    INCLUDE_DIR
    "${_tp_magic_enum}/magic_enum/include"
    AUTO_ADD_TO_DEPENDENCIES
    OFF
  )
  # NOT added to LOGGING_DEPENDENCY_INCLUDE_DIRS (which is PRIVATE, see
  # below): unlike fmt/loguru/glog/spdlog, magic_enum is used directly from
  # a PUBLIC header (include/util/string_util.h, behind #if
  # LOGGING_HAS_MAGICENUM) via #include <magic_enum/magic_enum.hpp>, so a
  # consumer including that header genuinely needs magic_enum's include dir
  # on their own path. Handled as its own PUBLIC entry near the main
  # target_include_directories() call, with its headers physically copied
  # into our own install tree (see the "--- Installation ---" section)
  # rather than exporting a separate magic_enum package dependency.
  set(LOGGING_MAGICENUM_INCLUDE_DIR "${_tp_magic_enum}/magic_enum/include")
endif()
if(LOGGING_BACKEND STREQUAL "LOGURU")
  add_third_party_library(
    loguru
    LIBRARY_TYPE
    STATIC
    SOURCE_DIR
    "${_tp_loguru}/loguru"
    FILTER_FLAGS
    "-flto=thin"
    "-flto=full"
    XSIGMA_TARGETS
    "Loguru::loguru=loguru::loguru|loguru"
    INCLUDE_DIR
    "${_tp_loguru}/loguru"
    AUTO_ADD_TO_DEPENDENCIES
    OFF
  )
  if(TARGET loguru)
    set_property(TARGET loguru PROPERTY POSITION_INDEPENDENT_CODE ON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(loguru PRIVATE -Wno-format-truncation)
    endif()
  endif()
  list(APPEND LOGGING_DEPENDENCY_INCLUDE_DIRS "${_tp_loguru}/loguru")
endif()
if(LOGGING_BACKEND STREQUAL "GLOG")
  add_third_party_library(
    glog
    LIBRARY_TYPE
    STATIC
    SOURCE_DIR
    "${_tp_glog}/glog"
    FILTER_FLAGS
    "-flto=thin"
    "-flto=full"
    XSIGMA_TARGETS
    "Glog::glog=glog::glog|glog"
    INCLUDE_DIR
    "${_tp_glog}/glog/src"
    AUTO_ADD_TO_DEPENDENCIES
    OFF
  )
  # add_third_party_library() tries an external (system) glog first when
  # XSIGMA_ENABLE_EXTERNAL is ON (the default) -- and this machine's Homebrew
  # glog is found, since XSigma's helper does a QUIET find_package(glog)
  # before ever looking at ThirdParty/glog. When that happens, only the
  # namespaced "glog::glog" target it exports exists; the bare "glog" target
  # is only defined by our own bundled CMakeLists.txt via add_subdirectory.
  # Appending our bundled ThirdParty/glog/glog/src unconditionally used to add
  # a second, stale glog/flags.h to the include path -- one whose GLOG_EXPORT
  # macro depends on a generate_export_header() output that only exists when
  # the bundled subdirectory was actually configured. That stale header could
  # shadow the external package's own (correctly export-macro'd) headers,
  # breaking the build. Only add it when the bundled copy is the one in use.
  if(TARGET glog)
    list(APPEND LOGGING_DEPENDENCY_INCLUDE_DIRS "${_tp_glog}/glog/src")
  endif()
endif()
if(LOGGING_BACKEND STREQUAL "SPDLOG")
  # spdlog as HEADER_ONLY: add_third_party_library only skips compilation for LIBRARY_TYPE
  # HEADER_ONLY (INTERFACE is treated as "default" and would add_subdirectory + build spdlog).
  add_third_party_library(
    spdlog
    LIBRARY_TYPE
    HEADER_ONLY
    SOURCE_DIR
    "${_tp_spdlog}/spdlog"
    XSIGMA_TARGETS
    "Spdlog::spdlog=spdlog::spdlog|spdlog"
    INCLUDE_DIR
    "${_tp_spdlog}/spdlog/include"
    AUTO_ADD_TO_DEPENDENCIES
    OFF
  )
  list(APPEND LOGGING_DEPENDENCY_INCLUDE_DIRS "${_tp_spdlog}/spdlog/include")
endif()

# --- Explicit source and header lists (Phase B: replacing glob pattern) ---
# Headers: Public API and internal headers
set(logging_headers
  # Public headers - the single logging API
  include/logger/logger.h
  include/logger/logger_verbosity_enum.h
  include/logger/config.h
  include/logger/structured.h
  include/logger/back_trace.h
  include/util/exception.h
  include/util/string_util.h
  include/util/env.h
  include/util/lazy.h

  # Public headers - common utilities
  include/common/logging_export.h
  include/common/logging_macros.h
  include/common/logging_pointer.h

  # Backend implementation headers (private, but listed so IDEs index them).
  # Exactly one concrete backend type is selected at compile time in backend.h.
  src/backend/backend.h
  src/backend/backend_types.h
  src/backend/backend_shared.h
  src/backend/loguru_backend.h
  src/backend/native_backend.h
  src/backend/spdlog_backend.h
  src/backend/glog_backend.h
)

# Sources: Implementation files (only non-test, non-vendor files)
set(logging_sources
  # Core implementation (moved to src/)
  src/logger.cpp
  src/back_trace.cpp
  src/exception.cpp
  src/string_util.cpp
  src/env.cpp
  # Backend implementations selected at compile time; reached via
  # backend::active_backend() (a singleton) instead of per-call #if.
  src/backend/factory.cpp
  src/backend/native_backend.cpp
  src/backend/spdlog_backend.cpp
  src/backend/loguru_backend.cpp
  src/backend/glog_backend.cpp
)

# --- Target definition ---
set(CMAKE_CXX_STANDARD ${LOGGING_CXX_STANDARD})

add_library(Logging ${logging_sources} ${logging_headers})
xsigma_target_apply_lto(Logging LOGGING)
if(LOGGING_ENABLE_SANITIZER)
  xsigma_configure_sanitizer(LOGGING)
endif()
xsigma_find_linker(${LOGGING_LINKER_CHOICE} Logging LOGGING_LTO_MODE)
xsigma_target_apply_cache(Logging LOGGING_ENABLE_CACHE LOGGING_CACHE_BACKEND)
xsigma_target_optional_icecc(Logging LOGGING_ENABLE_ICECC)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(Logging PUBLIC LOGGING_SHARED_DEFINE PRIVATE LOGGING_BUILDING_DLL)
  if(COMMAND xsigma_set_target_output_directories)
    xsigma_set_target_output_directories(Logging)
  endif()
else()
  target_compile_definitions(Logging PUBLIC LOGGING_STATIC_DEFINE)
endif()

add_library(Logging::Logging ALIAS Logging)

xsigma_module_create_filters("${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(
  Logging PROPERTIES CXX_STANDARD ${LOGGING_CXX_STANDARD} CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS
                                                                                   OFF
)

if(MSVC)
  target_compile_options(Logging PRIVATE /WX)
else()
  target_compile_options(Logging PRIVATE -include cstdlib)
endif()

# Clang-on-Windows (GNU driver + CodeView) can ICE in codegen when instantiating spdlog at -O3.
if(WIN32
   AND NOT MSVC
   AND CMAKE_CXX_COMPILER_ID MATCHES "Clang"
   AND LOGGING_BACKEND STREQUAL "SPDLOG"
)
  target_compile_options(
    Logging PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-O1>
  )
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/Testing/LoggingTest.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/Testing/LoggingTest.h" @ONLY
)

# Include roots:
# Build interface (source tree):
# - include/logger/: The single logging API - #include "include/logger/logger.h"
# - Repository root: also usable directly, e.g. #include "logger_verbosity_enum.h"
#   from within include/logger/ itself, and "src/backend/*.h" internally.
# Install interface (installed headers land at <prefix>/include/{common,logger,util}/*
# and <prefix>/include/logging.h -- see the install(DIRECTORY ...) call below):
# - "." (the install prefix itself): needed because every public header uses
#   repo-root-relative includes baked into the literal path string, e.g.
#   #include "include/common/logging_export.h" -- resolving that after
#   install requires the *parent* of the installed include/ tree, i.e. the
#   prefix, on the search path (not <prefix>/include).
# - include/logger: for the bare/sibling-relative includes used within
#   include/logger/ itself, e.g. logger.h's #include "logger_verbosity_enum.h".
# - LOGGING_MAGICENUM_INCLUDE_DIR (build) / CMAKE_INSTALL_INCLUDEDIR
#   (install): include/util/string_util.h is a public header and includes
#   <magic_enum/magic_enum.hpp> directly when LOGGING_HAS_MAGICENUM, so
#   magic_enum's include dir must be PUBLIC too -- unlike fmt/loguru/glog/
#   spdlog below. Its headers are physically copied into our own installed
#   include/ tree (see "--- Installation ---"), so the install-side entry is
#   just the standard include dir, not a separate exported dependency.
target_include_directories(
  Logging
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/logger>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<INSTALL_INTERFACE:.>
         $<INSTALL_INTERFACE:include/logger>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
if(LOGGING_ENABLE_MAGICENUM)
  target_include_directories(
    Logging PUBLIC $<BUILD_INTERFACE:${LOGGING_MAGICENUM_INCLUDE_DIR}>
                    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
endif()

if(LOGGING_DEPENDENCY_INCLUDE_DIRS)
  # PRIVATE: none of fmt/loguru/glog/spdlog's types appear in
  # Logging's own public headers (fmt is confined to string_util.cpp; the
  # active backend is an implementation detail behind logger::), so a
  # consumer linking Logging::Logging never needs these on their own include
  # path, and the installed package doesn't need to separately export them.
  target_include_directories(Logging PRIVATE ${LOGGING_DEPENDENCY_INCLUDE_DIRS})
endif()

if(TARGET Fmt::fmt)
  list(APPEND LOGGING_DEPENDENCY_LIBS Fmt::fmt)
endif()
# MagicEnum::magic_enum deliberately excluded from LOGGING_DEPENDENCY_LIBS
# (PRIVATE, see above) -- it needs to be a PUBLIC include dir instead; see
# LOGGING_MAGICENUM_INCLUDE_DIR near the main target_include_directories()
# call and the header install below.
if(TARGET Loguru::loguru)
  list(APPEND LOGGING_DEPENDENCY_LIBS Loguru::loguru)
endif()
if(TARGET Glog::glog)
  list(APPEND LOGGING_DEPENDENCY_LIBS Glog::glog)
endif()
if(TARGET Spdlog::spdlog)
  list(APPEND LOGGING_DEPENDENCY_LIBS Spdlog::spdlog)
endif()

if(LOGGING_ENABLE_CLANGTIDY)
  include(clang_tidy)
  xsigma_target_clang_tidy(Logging ${LOGGING_ENABLE_FIX})
endif()

if(LOGGING_ENABLE_IWYU)
  include(iwyu)
  xsigma_apply_iwyu(Logging)
endif()

if(LOGGING_DEPENDENCY_LIBS)
  # PRIVATE for the same reason as the include dirs above: these are
  # implementation details, not part of Logging's interface. Keeping them
  # PRIVATE also keeps them out of the exported LoggingTargets.cmake, so
  # find_package(Logging) in a downstream project never needs to separately
  # locate/export fmt, loguru, glog, spdlog, or magic_enum.
  target_link_libraries(Logging PRIVATE ${LOGGING_DEPENDENCY_LIBS})
endif()

if(LOGGING_DEPENDENCY_COMPILE_DEFINITIONS)
  target_compile_definitions(Logging PUBLIC ${LOGGING_DEPENDENCY_COMPILE_DEFINITIONS})
endif()

if(WIN32)
  # Own the DbgHelp dependency in the build system; do not rely on a source pragma.
  target_link_libraries(Logging PRIVATE dbghelp)
endif()

# --- Installation ---
install(
  TARGETS Logging
  EXPORT LoggingTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# Trailing slash on the source: installs include/'s *contents* (common/,
# logger/, util/, logging.h) directly under <prefix>/include, matching the
# INSTALL_INTERFACE "." + "include/logger" search roots set above -- not
# nested under an extra "Logging/include/" (which the target_include_
# directories() INSTALL_INTERFACE entries above do not account for, and
# which would leave every "include/..."-prefixed internal #include broken
# post-install).
install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
  PATTERN "*.h"
)
if(LOGGING_ENABLE_MAGICENUM)
  # include/util/string_util.h (public) includes <magic_enum/magic_enum.hpp>
  # directly -- physically copy magic_enum's headers into our own install
  # tree rather than exporting it as a separate find_package dependency
  # (it's header-only and vendored, so there's no compiled artifact or
  # upstream package identity to preserve).
  install(
    DIRECTORY "${LOGGING_MAGICENUM_INCLUDE_DIR}/magic_enum"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
    PATTERN "*.hpp"
  )
endif()

# --- CMake package config (find_package(Logging) support) ---
# LOGGING_DEPENDENCY_LIBS (fmt/loguru/glog/spdlog) are linked
# PRIVATE above and never appear in LoggingTargets.cmake's
# INTERFACE_LINK_LIBRARIES, so LoggingConfig.cmake does not need to also
# find_dependency() them -- they're an implementation detail baked into
# libLogging, not part of a consumer's link graph.
include(CMakePackageConfigHelpers)

set(LOGGING_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Logging")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/Cmake/LoggingConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/LoggingConfig.cmake"
  INSTALL_DESTINATION "${LOGGING_INSTALL_CMAKEDIR}"
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/LoggingConfigVersion.cmake"
  # Hardcoded rather than ${PROJECT_VERSION}: when embedded via
  # add_subdirectory (LOGGING_STANDALONE OFF), PROJECT_VERSION belongs to
  # whichever project() call is nearest/outermost, not this one's
  # project(Logging VERSION 1.0.0 ...) -- which only runs in the standalone
  # branch above. Keep in sync with that VERSION.
  VERSION 1.0.0
  COMPATIBILITY SameMajorVersion
)

install(
  EXPORT LoggingTargets
  FILE LoggingTargets.cmake
  NAMESPACE Logging::
  DESTINATION "${LOGGING_INSTALL_CMAKEDIR}"
)
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/LoggingConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/LoggingConfigVersion.cmake"
  DESTINATION "${LOGGING_INSTALL_CMAKEDIR}"
)

# --- Testing ---
if(LOGGING_ENABLE_TESTING)
  add_subdirectory(Testing/Cxx)
endif()

if(LOGGING_ENABLE_VALGRIND AND LOGGING_ENABLE_TESTING)
  xsigma_apply_valgrind_timeouts()
endif()

get_property(_logging_summary_backend CACHE LOGGING_BACKEND PROPERTY VALUE)
message("")
message("******** Logging module (CMake flags) ********")
message("  Backend          : ${_logging_summary_backend} (options: SPDLOG, LOGURU, GLOG, NATIVE)")
message("  Cxx standard     : ${LOGGING_CXX_STANDARD}")
message("  Cache            : ${LOGGING_ENABLE_CACHE}")
message("  Cache backend    : ${LOGGING_CACHE_BACKEND}")
message("  Icecc            : ${LOGGING_ENABLE_ICECC}")
xsigma_print_lto_linker_summary(LOGGING)
message("  Coverage         : ${LOGGING_ENABLE_COVERAGE}")
message("  Testing          : ${LOGGING_ENABLE_TESTING}")
message("  Examples         : ${LOGGING_ENABLE_EXAMPLES}")
message("  Gtest            : ${LOGGING_ENABLE_GTEST}")
message("  Benchmark        : ${LOGGING_ENABLE_BENCHMARK}")
message("  Magic enum       : ${LOGGING_ENABLE_MAGICENUM}")
message("  Cxa demangle     : ${LOGGING_ENABLE_CXA_DEMANGLE}")
message("  Portable floats  : ${LOGGING_ENABLE_PORTABLE_FLOAT_FORMAT}")
message("  Std format       : ${LOGGING_FORMAT_USE_STD}")
message("  Exception mode   : ${LOGGING_DEFAULT_EXCEPTION_MODE}")
message("  Clang-tidy       : ${LOGGING_ENABLE_CLANGTIDY}")
message("  Fix              : ${LOGGING_ENABLE_FIX}")
message("  Iwyu             : ${LOGGING_ENABLE_IWYU}")
message("  Sanitizer        : ${LOGGING_ENABLE_SANITIZER}")
message("  Sanitizer type   : ${LOGGING_SANITIZER_TYPE}")
message("  Spell            : ${LOGGING_ENABLE_SPELL}")
message("  Valgrind         : ${LOGGING_ENABLE_VALGRIND}")
message("")
message("Build:")
message("  Default C++ standard     : ${CMAKE_CXX_STANDARD}")
message("  C flags                  : ${CMAKE_C_FLAGS}")
message("  CXX flags                : ${CMAKE_CXX_FLAGS}")
message("  Shared linker flags      : ${CMAKE_SHARED_LINKER_FLAGS}")
message("  Static linker flags      : ${CMAKE_STATIC_LINKER_FLAGS}")
message("  Module linker flags      : ${CMAKE_MODULE_LINKER_FLAGS}")
get_directory_property(_summary_compile_defs DIRECTORY ${PROJECT_SOURCE_DIR} COMPILE_DEFINITIONS)
message("  Top-level compile defs   : ${_summary_compile_defs}")
message("")
