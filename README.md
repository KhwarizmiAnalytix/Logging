# Logging (`Library/Logging`)

**Structured logging**: levels, lazy evaluation, back traces, and a pluggable backend —
**Loguru** (default), **spdlog**, **glog**, or **native** (fmt-based).

## Layout

- `CMakeLists.txt` — `LOGGING_BACKEND`, `LOGGING_ENABLE_*`.
- `BUILD.bazel` — `//Library/Logging:Logging`; backend deps from `select`.
- `logger/` — public logger facade, verbosity enum, back traces.
- `util/` — exceptions, env helpers, string utilities, lazy values.
- `Testing/Cxx/` — unit tests and `BenchmarkLogger.cpp`.

Public C++ namespace: `logging`. Macros: `LOGGING_LOG_*`, `LOGGING_CHECK`, `LOGGING_THROW`.

Memory, Vectorization, and Core always link this library. Memory and Core use
`LOGGING_LOG_*` / `LOGGING_CHECK` directly; `VECTORIZATION_LOGF` /
`VECTORIZATION_CHECK` / `VECTORIZATION_THROW` forward here.

## CMake options

### Backend

| CMake variable | Default | Values |
|----------------|---------|--------|
| `LOGGING_BACKEND` | `LOGURU` | `NATIVE`, `LOGURU`, `GLOG`, `SPDLOG` — exactly one `LOGGING_HAS_*=1` |

Unknown values fail configure (`FATAL_ERROR`).

### Feature and toolchain

| CMake variable | Default | Summary |
|----------------|---------|---------|
| `LOGGING_ENABLE_MAGICENUM` | ON | `LOGGING_HAS_MAGICENUM` for enum ↔ string helpers |
| `LOGGING_ENABLE_CXA_DEMANGLE` | toolchain-dependent | Itanium ABI demangling; CMake verifies `<cxxabi.h>` |
| `LOGGING_ENABLE_PORTABLE_FLOAT_FORMAT` | OFF | Avoid floating-point `std::to_chars` overloads |
| `LOGGING_FORMAT_USE_STD` | OFF | Use `std::format`; requires C++20 and a supporting standard library |
| `LOGGING_DEFAULT_EXCEPTION_MODE` | `THROW` | `THROW` or `LOG_FATAL` before any runtime configuration |
| `LOGGING_ENABLE_TESTING` | ON | Tests |
| `LOGGING_ENABLE_GTEST` | ON | GoogleTest |
| `LOGGING_ENABLE_BENCHMARK` | ON | `benchmark_logging_logger` |
| Other `LOGGING_ENABLE_*` | see `CMakeLists.txt` | LTO, coverage, sanitizers, cache, … |

## Bazel flags

Starlark: [`bazel/logging.bzl`](../../bazel/logging.bzl).

| Define | Effect |
|--------|--------|
| *(unset)* | loguru |
| `logging_backend=glog\|loguru\|native\|spdlog` | Matching `LOGGING_HAS_*` |
| `disable_magic_enum` | `LOGGING_HAS_MAGICENUM=0` |
| `logging_enable_cxa_demangle=false` | `LOGGING_HAS_CXA_DEMANGLE=0` |
| `logging_enable_portable_float_format=true` | Portable floating-point formatter |
| `logging_format_use_std=true` | C++20 `std::format` formatter |
| `logging_default_exception_mode=log_fatal` | Default exception mode is `LOG_FATAL` |

Use `--config=logging_loguru` (default), `logging_spdlog`, `logging_glog`, or
`logging_native` from `.bazelrc`; the feature defines above also have matching
`logging_portable_float_format`, `logging_std_format`, `logging_default_log_fatal`,
and `logging_no_cxa_demangle` configs.

## Public API (abridged)

```cpp
#include "logger/logger.h"
#include "util/exception.h"

logging::logger::init();
logging::logger::set_stderr_verbosity(logging::logger_verbosity_enum::VERBOSITY_INFO);
logging::logger::log_to_file("app.log", logging::logger::file_mode::truncate,
                             logging::logger_verbosity_enum::VERBOSITY_INFO);

LOGGING_LOG_INFO("started {}", name);
LOGGING_CHECK(ptr != nullptr, "ptr was null");
```

`LOGGING_LOG_FATAL` and `exception_mode::LOG_FATAL` abort the process after emitting the message.

Environment: `LOGGING_EXCEPTION_MODE=THROW|LOG_FATAL`.

## Benchmarks

```bash
cd Scripts
python3 setup.py config.build.ninja.clang.release.benchmark --project.logging
# from repo root:
build_ninja_project_logging_logging_loguru/bin/benchmark_logging_logger --benchmark_min_time=0.5s
```

Reconfigure with `--logging=SPDLOG|GLOG|NATIVE` to compare backends. Numbers and
methodology: [Docs/readme/logging.md](../../Docs/readme/logging.md).
