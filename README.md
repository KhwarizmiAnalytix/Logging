# Logging

[![CI](https://github.com/KhwarizmiAnalytix/Logging/actions/workflows/ci.yml/badge.svg)](https://github.com/KhwarizmiAnalytix/Logging/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/KhwarizmiAnalytix/Logging/branch/main/graph/badge.svg)](https://codecov.io/gh/KhwarizmiAnalytix/Logging)
[![License: GPL v3 / Commercial](https://img.shields.io/badge/license-GPL--3.0--or--later%20%2F%20commercial-blue.svg)](LICENSE)

**Production-grade C++ structured logging**: levels, lazy evaluation, backtraces, and pluggable backends — **Loguru** (default), **spdlog**, **glog**, or **native** (fmt-based).

A standalone, zero-dependency CMake library for any C++ project. Compile-time backend selection; same public API across all backends.

## Quick Start (Third-Party Integration)

### Add to your CMake project

```cmake
# Option 1: Embedded (git submodule or downloaded)
add_subdirectory(Logging)
target_link_libraries(MyApp PRIVATE Logging::Logging)

# Option 2: Installed package
find_package(Logging CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE Logging::Logging)
```

### Initialize and use in C++

```cpp
#include "include/logging.h"

// One-time setup (at program start)
logging::logger::init();
logging::logger::set_stderr_verbosity(logging::logger_verbosity_enum::VERBOSITY_INFO);
logging::logger::log_to_file("app.log", logging::logger::file_mode::truncate,
                             logging::logger_verbosity_enum::VERBOSITY_INFO);

// Log at any point in your code
LOGGING_LOG_INFO("Application started with {} threads", num_threads);
LOGGING_LOG_DEBUG("Processing item {} of {}", i, total);
LOGGING_LOG_WARN("Unusual condition detected: {}", message);

// Assertions with logging (aborts if false)
LOGGING_CHECK(ptr != nullptr, "Pointer was null");

// Throw an exception or abort (depends on configuration)
if (error_condition) {
    LOGGING_THROW(std::runtime_error, "Fatal error: {}", reason);
}
```

### Choose a backend

Set `LOGGING_BACKEND` when configuring (default: `LOGURU`):

```bash
# Loguru (structured, context-aware logging; default)
cmake -S . -B build

# spdlog (high-performance, widely used)
cmake -S . -B build -DLOGGING_BACKEND=SPDLOG

# Native (minimal, fmt-based formatter; no dependencies)
cmake -S . -B build -DLOGGING_BACKEND=NATIVE

# Glog (Google's logging; integrates with gflags)
cmake -S . -B build -DLOGGING_BACKEND=GLOG
```

All backends expose the same public API; backend selection is a build-time configuration (not a runtime choice).

---

## What You Get

**Public macros** (backend-agnostic):

| Macro | Purpose | Behavior |
|-------|---------|----------|
| `LOGGING_LOG_FATAL` | Always-logged fatal message | Aborts process after logging |
| `LOGGING_LOG_ERROR` | Error (verbosity: ERROR) | Logged only if severity ≥ ERROR |
| `LOGGING_LOG_WARN` | Warning (verbosity: WARN) | Logged only if severity ≥ WARN |
| `LOGGING_LOG_INFO` | Informational (verbosity: INFO) | Logged only if severity ≥ INFO |
| `LOGGING_LOG_DEBUG` | Debug (verbosity: DEBUG) | Logged only if severity ≥ DEBUG |
| `LOGGING_LOG_VERBOSE` | Trace (verbosity: VERBOSE) | Most detailed; rarely used |
| `LOGGING_CHECK(cond, msg)` | Assertion with logging | Aborts if false; logs message |
| `LOGGING_THROW(exception, msg)` | Throw or log-abort | Configured to throw or call `LOG_FATAL` |

**Guarantees**:
- **Format-string compatible**: Uses `fmt` syntax (`"{}"`, `"{:04d}"`, etc.); portable across all backends.
- **Lazy evaluation**: Format arguments are only evaluated if the log level is enabled; no performance penalty for disabled logs.
- **Backtraces**: Each log entry captures (optionally) the call stack; useful for post-mortem debugging.
- **Thread-safe**: Multiple threads can log simultaneously; backend guarantees no interleaving corruption.
- **Exception mode configurable**: `LOGGING_THROW` either throws exceptions or calls `LOG_FATAL` (abort); set via CMake flag or environment variable.

**Compile-time decisions** (not runtime branching):
- Which backend is active (Loguru, spdlog, glog, native)
- Whether to use std::format (C++20) or fmt library
- Whether to demangle symbol names in backtraces
- Default exception mode (throw vs. abort)

**Runtime configuration**:
- Verbosity level (per destination: stderr, file, or custom handler)
- Log destination (stderr, file, or custom callback)
- Format template (depends on backend)

---

## Public API

### Logging macros

```cpp
LOGGING_LOG_INFO("started {}", name);
LOGGING_LOG_DEBUG("processing item {} of {}", i, total);
LOGGING_LOG_WARN("unusual condition: {}", detail);
LOGGING_LOG_ERROR("failed to open file: {}", filename);
LOGGING_LOG_FATAL("irrecoverable error: {}", reason);  // aborts

LOGGING_CHECK(ptr != nullptr, "ptr is null");         // aborts if false

LOGGING_THROW(std::runtime_error, "message: {}", arg); // throws or aborts
```

### Runtime configuration

```cpp
// Initialize (must call once before logging)
logging::logger::init();

// Set stderr verbosity
logging::logger::set_stderr_verbosity(logging::logger_verbosity_enum::VERBOSITY_INFO);

// Log to file
logging::logger::log_to_file("app.log",
    logging::logger::file_mode::truncate,
    logging::logger_verbosity_enum::VERBOSITY_DEBUG);

// Flush all pending writes
logging::logger::flush();
```

### Environment variables

| Variable | Values | Effect |
|----------|--------|--------|
| `LOGGING_EXCEPTION_MODE` | `THROW`, `LOG_FATAL` | How `LOGGING_THROW` behaves; overrides CMake default |

---

## Layout

- `CMakeLists.txt` — backend selection (`LOGGING_BACKEND`), feature flags (`LOGGING_ENABLE_*`).
- `include/logging.h` — umbrella header; only one needed for client code.
- `include/logger/` — logger facade (static methods), verbosity enum, backtrace support.
- `include/util/` — exceptions, environment helpers, string utilities, lazy values.
- `Testing/Cxx/` — unit tests and benchmarks.
- `ThirdParty/` — vendored nested dependencies (fmt, loguru, glog, spdlog, magic_enum, googletest, benchmark).

Public C++ namespace: `logging`. Compile-time definitions: `LOGGING_HAS_LOGURU`, `LOGGING_HAS_SPDLOG`, `LOGGING_HAS_GLOG`, `LOGGING_HAS_NATIVE` (exactly one set to 1).

---

## CMake options

### Backend selection

| CMake variable | Default | Values |
|----------------|---------|--------|
| `LOGGING_BACKEND` | `LOGURU` | `NATIVE`, `LOGURU`, `GLOG`, `SPDLOG` — mutually exclusive; exactly one `LOGGING_HAS_*=1` |

Unknown values fail configure.

### Feature flags

| CMake variable | Default | Summary |
|----------------|---------|---------|
| `LOGGING_ENABLE_MAGICENUM` | ON | Enum ↔ string helpers via magic_enum |
| `LOGGING_ENABLE_CXA_DEMANGLE` | Toolchain-dependent | Itanium ABI demangling in backtraces |
| `LOGGING_ENABLE_PORTABLE_FLOAT_FORMAT` | OFF | Avoid `std::to_chars` floating-point overloads (compatibility) |
| `LOGGING_FORMAT_USE_STD` | OFF | Use C++20 `std::format` instead of fmt |
| `LOGGING_DEFAULT_EXCEPTION_MODE` | `THROW` | How `LOGGING_THROW` behaves before runtime config: `THROW` or `LOG_FATAL` |
| `LOGGING_ENABLE_TESTING` | ON | Tests |
| `LOGGING_ENABLE_GTEST` | ON | GoogleTest |
| `LOGGING_ENABLE_BENCHMARK` | ON | Benchmark suite |

### Toolchain

Standard flags available: LTO, coverage, sanitizers, compiler cache, clang-tidy. See `CMakeLists.txt` for full list.

---

## Bazel support

Located in `bazel/logging.bzl`. Use `--config=logging_loguru|logging_spdlog|logging_glog|logging_native` to select backend.

**Known gap**: `logging_backend=glog` fails under Bazel (glog's BUILD.bazel depends on gflags, not declared here; CMake uses `WITH_GFLAGS=OFF` instead). Loguru, spdlog, and native work fine under Bazel.

---

## CI & Coverage

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs on every push/PR to `main`:

| Job | Platforms | Coverage |
|-----|-----------|----------|
| `linux` | CMake+Ninja, gcc & clang, all four backends (Release), Debug canary | All backends |
| `macos` | CMake+Ninja, AppleClang, all four backends | All backends |
| `windows` | CMake+MSVC, all four backends | All backends |
| `bazel` | Bazel build+test | loguru, spdlog, native (not glog; see note above) |
| `sanitize` | ASan, UBSan | Loguru backend |
| `coverage` | gcc+gcov/lcov uploaded to Codecov | — |
| `lintrunner` | clang-format, clang-tidy, cmake-format, codespell, editorconfig | — |

See [CONTRIBUTING.md](CONTRIBUTING.md) for running checks locally.

---

## Platform and compiler support

- **C++ standard**: C++17 minimum (configurable up to C++23)
- **Compilers**: GCC 8+, Clang 7+, MSVC 2019+, AppleClang 12+
- **OS**: Linux, macOS, Windows, plus any POSIX-compliant system
- **Dependencies**: Vendored (fmt, magic_enum); no external requirement

---

## Troubleshooting

### Backend silently falls back to NATIVE when I request OpenMP / other backend

Most common cause: missing development headers (e.g., `libomp-dev` on Linux, or Xcode Command Line Tools on macOS). Inspect CMake configure output or the compiled build log. Check `CMakeLists.txt` for how each backend verifies its availability; reconfigure with your dev tools installed.

### Can't use Glog backend under Bazel

Currently unsupported; use loguru, spdlog, or native instead. CMake handles glog fine by disabling gflags.

### Exception mode: when does `LOGGING_THROW` actually throw?

Depends on `LOGGING_DEFAULT_EXCEPTION_MODE` (CMake, at compile time) or `LOGGING_EXCEPTION_MODE` (environment variable, at runtime). Set to `THROW` for exceptions, `LOG_FATAL` to abort instead. Environment variable overrides CMake default.

---

## Examples

```cpp
#include "include/logging.h"

int main() {
    logging::logger::init();
    logging::logger::set_stderr_verbosity(logging::logger_verbosity_enum::VERBOSITY_DEBUG);

    LOGGING_LOG_INFO("Application started");

    for (int i = 0; i < 100; ++i) {
        if (i % 10 == 0) {
            LOGGING_LOG_DEBUG("Iteration {}", i);
        }
    }

    try {
        LOGGING_CHECK(true, "this always passes");
        LOGGING_LOG_INFO("All checks passed");
    } catch (const std::exception& e) {
        LOGGING_LOG_ERROR("Caught exception: {}", e.what());
    }

    LOGGING_LOG_FATAL("Done");  // aborts here
}
```

---

## See Also

- [Benchmarks](BENCHMARKS.md) — performance comparison across backends
- [CONTRIBUTING.md](CONTRIBUTING.md) — how to build, test, and contribute locally
- [LICENSE](LICENSE) — dual-license (GPLv3 or commercial)
