# Logging

[![CI](https://github.com/KhwarizmiAnalytix/Logging/actions/workflows/ci.yml/badge.svg)](https://github.com/KhwarizmiAnalytix/Logging/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/KhwarizmiAnalytix/Logging/branch/main/graph/badge.svg)](https://codecov.io/gh/KhwarizmiAnalytix/Logging)
[![License: GPL v3 / Commercial](https://img.shields.io/badge/license-GPL--3.0--or--later%20%2F%20commercial-blue.svg)](LICENSE)

C++ text logging with compile-time selection of **Loguru** (default),
**spdlog**, **glog**, or a **native** implementation. The library includes
verbosity filtering, lazy macro arguments, scopes, callbacks, exceptions,
and backtrace utilities. Dependencies are vendored under `ThirdParty/`.

The backends share a public facade but differ in behavior. Start with the
[design documentation](docs/logging_design.md) for the current architecture, backend
comparison, review findings, and proposed refactoring.

## Quick start

Embed the repository in a CMake project:

```cmake
add_subdirectory(Logging)
target_link_libraries(MyApp PRIVATE Logging::Logging)
```

The current installation rules do not generate a complete CMake config package;
`find_package(Logging CONFIG REQUIRED)` is not yet a supported standalone
installation path.

```cpp
#include "include/logging.h"

int main()
{
    logging::logger::set_enable_unsafe_signal_handler(false);
    logging::logger::init();
    logging::logger::set_stderr_verbosity(
        logging::logger_verbosity_enum::VERBOSITY_INFO);
    logging::logger::log_to_file("app.log",
        logging::logger::file_mode::append,
        logging::logger_verbosity_enum::VERBOSITY_INFO);

    LOGGING_LOG_INFO("Application started with {} threads", 4);
    LOGGING_LOG_WARNING("Retry attempt {}", 1);
    LOGGING_LOG_DEBUG(INFO, "Only emitted in a debug build");

    logging::logger::flush();
    logging::logger::end_log_to_file("app.log");
    return 0;
}
```

Configure signals and initialization during serialized startup. Logging can
work before explicit initialization on some backends; `init()` also controls
backend startup behavior and optional signal installation.

## Public API

| API | Behavior |
|-----|----------|
| `LOGGING_LOG_INFO`, `LOGGING_LOG_WARNING`, `LOGGING_LOG_ERROR` | Format and emit when verbosity passes the cutoff |
| `LOGGING_LOG(INFO, ...)` | Generic named-verbosity macro |
| `LOGGING_LOG_DEBUG(INFO, ...)` | Same operation, removed under `NDEBUG` |
| `LOGGING_LOG_IF(INFO, condition, ...)` | Conditional logging with a named verbosity |
| `LOGGING_VLOG_IF(1, condition, ...)` | Conditional logging with numeric verbosity |
| `LOGGING_LOG_SCOPE_FUNCTION(INFO)` | Function scope entry/exit using RAII |
| `LOGGING_LOG_START_SCOPE(INFO, "id")`, `LOGGING_LOG_END_SCOPE("id")` | Manual named scopes |
| `LOGGING_LOG_FATAL(...)` | Aborts if dispatched; currently suppressed by an OFF macro cutoff |
| `LOGGING_THROW("message {}", value)` | Throws `logging::exception` or logs and aborts, depending on exception mode |
| `LOGGING_CHECK(condition, ...)` | Uses the same throw/abort policy when the condition fails |

The numeric levels are FATAL (-3), ERROR (-2), WARNING (-1), INFO (0), and
TRACE/MAX (9), plus OFF (-9) and INVALID (-10) control values. Lower values
are more severe; messages pass when `verbosity <= cutoff`. DEBUG is a
build-mode macro guard, not an enum value.

Public formatting converts arguments to strings before calling fmt or
std-format. Use `{}` substitutions; general numeric format specifiers and
compile-time format checking are not provided. Disabled logging macros avoid
format-argument evaluation, but still query the cutoff.

`logger::Message` owns its strings. Callbacks receive
`void handler(void* user_data, const logger::Message&)` and must copy fields
to retain them. Current callback lifetime/reentrancy limitations and glog's
unsupported callback behavior are documented in the
[architecture review](docs/logging_design.md#current-architecture-and-review).

Backtraces are available through `include/logger/back_trace.h` and exception
construction. Ordinary log records do not automatically capture a stack.

## Configuration and backend selection

```sh
cmake -S . -B build -DLOGGING_BACKEND=NATIVE
cmake --build build
ctest --test-dir build --output-on-failure
```

Use `LOGURU`, `SPDLOG`, `GLOG`, or `NATIVE`; unknown values fail configuration.
Selection is a build-time choice. Each build defines exactly one
`LOGGING_HAS_*` backend macro to 1 and the others to 0.

| CMake variable | Default | Purpose |
|----------------|---------|---------|
| `LOGGING_BACKEND` | `LOGURU` | Select backend |
| `LOGGING_CXX_STANDARD` | `20` | Language standard; sources require C++17 facilities |
| `LOGGING_ENABLE_MAGICENUM` | ON | Enum/string helpers |
| `LOGGING_ENABLE_CXA_DEMANGLE` | Toolchain-dependent | Symbol demangling |
| `LOGGING_ENABLE_PORTABLE_FLOAT_FORMAT` | OFF | Floating-point conversion fallback |
| `LOGGING_FORMAT_USE_STD` | OFF | Select std-format in string utilities; requires C++20 library support |
| `LOGGING_DEFAULT_EXCEPTION_MODE` | `THROW` | `THROW` or `LOG_FATAL` |
| `LOGGING_ENABLE_TESTING` | ON | Build tests |
| `LOGGING_ENABLE_GTEST` | ON | GoogleTest support |
| `LOGGING_ENABLE_BENCHMARK` | ON | Build benchmarks |

The native backend still uses fmt internally. Selecting std-format does not
remove all fmt dependencies. CMake requires version 3.16 or later; see the
[build file](CMakeLists.txt) for toolchain checks and additional options.

`LOGGING_EXCEPTION_MODE=THROW` or `LOGGING_EXCEPTION_MODE=LOG_FATAL` overrides
the compiled default when the exception mode is first read. Calling
`logging::set_exception_mode(...)` initializes/updates the runtime mode.

Bazel selection uses a define:

```sh
bazel build //:Logging --define=logging_backend=native
bazel test //... --define=logging_backend=native --test_output=errors
```

The glog Bazel dependency has a known gflags integration gap; CMake configures
glog with `WITH_GFLAGS=OFF`. Consult [CONTRIBUTING.md](CONTRIBUTING.md) for the
setup helpers and local checks.

## Design and validation

The current [CI workflow](.github/workflows/ci.yml) configures CMake tests for
all four backends on Linux, macOS, and Windows. Bazel jobs use the default
backend. ASan/UBSan jobs use Loguru; this is not full concurrency coverage.

- [Logging design](docs/logging_design.md) — architecture, contracts, and compatibility
- [Detailed implementation phases](docs/logging_design.md#implementation-phases)
- [Verification strategy](docs/logging_design.md#verification-strategy)
- [Contribution guide](CONTRIBUTING.md) and [license](LICENSE)
