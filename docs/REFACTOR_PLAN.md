# Logging Library Refactoring — Implementation Roadmap

**Status:** Phase A (Directory Layout & Enums) — In Progress  
**Date:** 2026-09-25  
**Based on:** External architecture review + user approval (2026-09-25)

---

## Overview

This document supersedes `PHASE1_BASELINE_REPORT.md` and `PHASE2_IMPLEMENTATION_PLAN.md`. It outlines a systematic refactor across five phases to address the architecture review's findings and move the library toward production-grade quality.

**Critical finding that was already fixed:** Named-scope global state now uses `thread_local` stacks instead of a mutex-guarded `unordered_map<thread::id, vector<...>>` (session start, pre-planning).

---

## Phase A: Directory Layout, `level` Enum, Printf Removal, Backend Split

### Goal
Eliminate the 1512-line monolithic `logger.cpp`; introduce a library-owned severity enum; remove printf formatting from the public API; separate backend implementations; move `.cpp` files out of `include/`.

### Status: IN PROGRESS ✏️

#### A1. New Severity Enum — `include/logging/level.h`
- **Created:** `include/logging/level.h` with library-owned `enum class level { trace, debug, info, warn, error, critical, off }`
- **Compat path:** `logging::deprecated::logger_verbosity_enum` alias for backward compatibility
- **Mapping:**
  - `level::off` ← `VERBOSITY_OFF` (was: -9)
  - `level::critical` ← `VERBOSITY_FATAL` (was: -3)
  - `level::error` ← `VERBOSITY_ERROR` (was: -2)
  - `level::warn` ← `VERBOSITY_WARNING` (was: -1)
  - `level::info` ← `VERBOSITY_INFO` (was: 0)
  - `level::trace` ← `VERBOSITY_TRACE` (was: 9)

#### A2. New Public API Header — `include/logging/logger.h`
- **Created:** new public header using `level`, not `logger_verbosity_enum`
- **Key changes:**
  - `set_stderr_verbosity(level)` + deprecated overload `(logger_verbosity_enum)`
  - `set_internal_verbosity_level(level)` + deprecated overload
  - `log_to_file(..., level)` + deprecated overload
  - `add_callback(..., level, ...)` + deprecated overload
  - `log(..., level, ...)` + deprecated overload
  - `start_scope(..., level, ...)` + deprecated overload
  - `struct signal_options { install_handlers = false, sigabrt, sigbus, ... }` replacing public static bools (see A4 below)
  - `Message::severity` is now `level`, with `verbosity_deprecated()` accessor for compat
  - No `log_f` / `start_scope_f` (printf-style removed)
  - Simplified `log_scope_raii(level, fname, lineno)` constructor (no printf variadic)
  - Macros updated: `LOGGING_LOG(...)` now maps to `level::info`, `level::warn`, etc.

#### A3. Signal Handlers → Configuration Struct
- **Old:** Public static mutable bools (`enable_unsafe_signal_handler`, `enable_sig{abrt,bus,...}_handler`)
- **New:** `signal_options` struct passed to `init(..., const signal_options&)`
- **Defaults:** `install_handlers = false` (was: `enable_unsafe_signal_handler = true`) — libraries should NOT seize process-wide signals by default
- **Compat:** Keep `set_enable_unsafe_signal_handler(bool)` / `get_...()` free functions that map to `signal_options.install_handlers`

#### A4. Directory Reorganization
Planned (not yet applied):
```
Old structure:                  New structure:
include/logging.h       →       include/logging/logging.h (facade)
include/logger/                 include/logging/
  logger.h            →           logger.h (public API, declarations)
  logger.cpp          →           (moved to src/logger.cpp)
  logger_verbosity_enum.h  →       (moved to level.h)
  back_trace.h/.cpp   →           back_trace.h/.cpp

include/util/                   include/logging/
  exception.h/.cpp    →           exception.h/.cpp
  string_util.h/.cpp  →           string_util.h/.cpp
  env.h/.cpp          →           env.h/.cpp
  lazy.h              →           lazy.h

include/common/                 (keep as-is)
  logging_export.h
  logging_macros.h
  logging_pointer.h

src/ (new)
  logger.cpp
  exception.cpp
  string_util.cpp
  back_trace.cpp
  env.cpp
  
src/backend/ (new)
  backend.h             (compile-time backend contract)
  native_backend.cpp
  spdlog_backend.cpp
  loguru_backend.cpp
  glog_backend.cpp
```

#### A5. Backend Interface Extraction — `src/backend/backend.h`
Planned (not yet applied):
```cpp
namespace logging::backend {
  // Compile-time-selected backend, no virtual dispatch
  // Methods called by logger.cpp dispatcher
  void log(level, const char* fname, unsigned line, const char* txt);
  void flush();
  void set_cutoff(level);
  void add_callback(const char* id, log_handler_callback_t, void* data, level);
  void remove_callback(const char* id);
  void start_scope(level, const char* id, const char* fname, unsigned line);
  void end_scope(const char* id);
}
```

#### A6. Printf API Removal
Planned (not yet applied):
- Remove from `logger.h` and `logger.cpp`:
  - `log_f(..., const char* fmt, ...)`
  - `start_scope_f(..., const char* fmt, ...)`
  - `LOGGING_PRINTF_LIKE` macros
  - `LOGGING_FORMAT_STRING_TYPE` platform macros
- Scope entry formatting: use `logging::strings::format("{}", __func__)` instead of printf `"%s"` within macros
- Existing test code using these will need updates (affects `Testing/Cxx/`)

### Remaining Work in Phase A
1. **Update all includes** across the repo:
   - `#include "include/logger/logger.h"` → `#include "logging/logger.h"`
   - `#include "include/util/exception.h"` → `#include "logging/exception.h"`
   - `#include "include/util/string_util.h"` → `#include "logging/string_util.h"`
   - `#include "include/logger/back_trace.h"` → `#include "logging/back_trace.h"`
   - `#include "include/util/env.h"` → `#include "logging/env.h"`
   - `#include "include/util/lazy.h"` → `#include "logging/lazy.h"`
   - In `Testing/Cxx/*.cpp`: update includes (8 of 11 files affected)
   - In `include/logging/logging.h` facade: already correct
   - In new `src/**/*.cpp`: update includes to use new paths

2. **Update old logger.h/logger.cpp** in `include/logger/`:
   - Delete or mark as deprecated (leave in place for backward-compat include)
   - Or: remove entirely once Phase B updates CMake/Bazel

3. **Migrate logger.cpp implementation**:
   - Add overloads accepting `level` alongside `logger_verbosity_enum`
   - Add `convert_to_level(int)` and `convert_to_level(const char*)`
   - Add `get_current_verbosity_cutoff()` returning `level` (keep old variant returning `logger_verbosity_enum`)
   - Update internal scope tracking to work with both enum types (adapters)

4. **Migrate exception.cpp, string_util.cpp, etc.** in `src/`:
   - Update includes to use new `include/logging/` paths
   - No API changes to these modules, just location/includes

5. **Create scope.cpp** (optional but recommended):
   - Extract thread-local scope stacks and manipulation (`push_named_scope`, `pop_named_scope`, `loguru_push_scope`, `loguru_pop_scope`) from logger.cpp to a dedicated translation unit
   - Reduces logger.cpp complexity further

### Verification (Phase A Complete)
- [ ] CMake configures and builds NATIVE and SPDLOG backends
- [ ] Tests pass: `TestLogger`, `TestLoggerDisableSignalHandler`, `TestLoggerThreadName`
- [ ] Clang-tidy runs with no new issues (`.claude/skills/clang-tidy`)
- [ ] Session checklist passes (`.claude/skills/session-checklist`)

---

## Phase B: Build System — Explicit Sources & Install Layout

### Goal
Replace fragile recursive `file(GLOB_RECURSE ...)` with explicit source lists; normalize include conventions; align CMake and Bazel.

### Planned Changes

1. **CMakeLists.txt** (root, ~line 447-471):
   ```cmake
   # BEFORE: file(GLOB_RECURSE logging_headers ...)
   #         file(GLOB_RECURSE logging_sources ...)
   #         exclude Testing, ThirdParty, CMakeFiles
   
   # AFTER: target_sources(Logging PRIVATE
   #            src/logger.cpp
   #            src/exception.cpp
   #            src/string_util.cpp
   #            src/back_trace.cpp
   #            src/env.cpp
   #            src/backend/${LOGGING_BACKEND_SELECTED}_backend.cpp
   #        )
   ```

2. **Include directories** (~line 530-534):
   ```cmake
   # BEFORE: PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
   #                ${CMAKE_CURRENT_SOURCE_DIR}/include/logger
   
   # AFTER:  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
   #                (no special /include/logger needed once includes are normalized)
   ```

3. **Install headers** (~line 589-594):
   ```cmake
   # BEFORE: install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include"
   #                 DESTINATION include/Logging ...)
   #                 then: #include "include/logging.h"
   
   # AFTER:  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/logging"
   #                 DESTINATION include ...)
   #                 then: #include <logging/logging.h>
   ```

4. **BUILD.bazel** (root):
   - Replace glob'd `logging_hdrs`/`logging_srcs` filegroups with explicit lists
   - Add explicit `include/logging/` list
   - Mirror backend selection: `src/backend/${backend}_backend.cpp`
   - Drop `includes = ["include/logger"]` (no longer needed)

### Verification (Phase B Complete)
- [ ] CMake full rebuild: both backends
- [ ] Bazel full build: both backends
- [ ] Installed tree is consumable as `#include <logging/logging.h>`
- [ ] All tests pass post-install

---

## Phase C: Hot-Path Redesign — Typed Format Arguments

### Goal
Eliminate intermediate `std::string` allocation on the hot path for backends that support direct formatting (spdlog).

### Planned Changes
- Add templated `logging::log<level::L>(source_location, fmt::format_string<Args...>, Args&&...)`
- Backend adapters: spdlog forwards args directly; native/loguru/glog format internally
- Macros refactored to call templated `log<>` instead of `strings::format().c_str()`
- Preserve lazy evaluation (cutoff check first)
- Keep pre-formatted `logger::log(..., const char* txt)` for callback/scope-exit messages

### Verification (Phase C Complete)
- [ ] Macro-level API source-compatible (call sites unchanged)
- [ ] Benchmark: SPDLOG enabled-INFO allocations reduced
- [ ] Tests pass unchanged

---

## Phase D: Structured Logging, Callback Reentrancy, Opt-in Signals

### Goal
Add structured (key-value) logging; harden callback lifetime/reentrancy; finish signal-handler opt-in.

### Key Fixes
1. **Callback reentrancy:**
   - `native_flush()`: move callback invocations outside `g_io_mutex` (snapshot-then-invoke pattern)
   - `native_remove_callback()`: same fix
   - Add `thread_local bool in_user_callback` guard to block nested callback dispatch
   - Allow nested logging to reach sinks (guard checks `in_user_callback`)

2. **Structured logging:**
   - `include/logging/record.h`: new `log_record` struct with `std::span<const field>` fields
   - `kv()` helper: build key-value pairs
   - `LOGGING_LOG_INFO_KV("title", kv("key", val), ...)`  macros
   - Backends render fields as `key=value` suffixes or structured output

3. **Opt-in signals:** (mostly covered in Phase A.4)
   - Default `signal_options.install_handlers = false`

### Verification (Phase D Complete)
- [ ] New `TestCallbackReentrancy.cpp` passes (nested logging, no deadlock/recursion)
- [ ] Concurrent callback add/remove under TSan
- [ ] Structured logging macros compile and render expected output

---

## Phase E: Exception Framework Boundary Decision

### Goal
Document the architectural boundary of the exception framework.

### Decision
Keep `logging::exception` / `LOGGING_THROW` / `LOGGING_CHECK` in this repo for now (not a separate target), but:
- Backtraces are opt-in on error/fatal/exception paths only (never on INFO/DEBUG hot paths)
- Exception framework is logically separate from core logging (document this)
- Flag as future split candidate if the repo becomes foundational infrastructure

### Verification (Phase E Complete)
- [ ] `docs/REFACTOR_PLAN.md` updated with boundary notes
- [ ] Confirm `back_trace::capture_on_error()` not reachable from non-error paths
- [ ] Memory notes updated

---

## Backward Compatibility & Deprecation

Throughout phases A–D, old APIs remain available via:
- `logger_verbosity_enum` alias to deprecated type
- Overloads accepting old enum alongside new `level` type
- Wrapper functions (`set_enable_unsafe_signal_handler` → `signal_options.install_handlers`)
- `Message::verbosity_deprecated()` accessor

Source code depending on old names will still compile (with deprecation warnings once added via CMake flags).

---

## Testing Strategy

### Unit Tests
- Existing `Testing/Cxx/Test*.cpp` files: update includes, run to verify no regressions
- New `TestCallbackReentrancy.cpp`: reentrancy guards, nested logging
- New benchmark extending `BenchmarkLogger.cpp`: allocation metrics post-hot-path redesign

### Build Validation
- `.claude/skills/project-build config.build.test` per backend (NATIVE, SPDLOG; attempt LOGURU/GLOG)
- `.claude/skills/clang-tidy` on modified files
- `.claude/skills/session-checklist` before each phase end

### Integration
- Install artifacts: verify `#include <logging/logging.h>` works from a consumer
- TSan on callback concurrency tests (Phase D)

---

## Timeline & Phasing

Each phase is independently committable and testable:

| Phase | Scope | Est. Effort | Blocker | Status |
|-------|-------|------------|---------|--------|
| A | Dirs, enums, printf removal | Large | None | 📝 IN PROGRESS |
| B | CMake/Bazel explicit sources | Medium | Phase A complete | ⏳ READY |
| C | Hot-path templates | Medium | Phase A complete | ⏳ READY |
| D | Struct logging, reentrancy | Medium | Phase B complete (build) | ⏳ READY |
| E | Exception boundary doc | Small | Phase D complete | ⏳ READY |

---

## Success Criteria

- [x] Thread-local scopes (pre-phase): ✅ Complete
- [ ] Phase A: New headers, level enum, backward compat (in progress)
- [ ] Phase B: Build system explicit, install layout (ready)
- [ ] Phase C: Typed fmt arguments (ready)
- [ ] Phase D: Structured kv(), callback reentrancy (ready)
- [ ] Phase E: Exception boundary documented (ready)
- [ ] All tests pass
- [ ] Clang-tidy, session-checklist clean
- [ ] Installation + consumer build succeeds

---

## Known Issues & Risks

### Medium Risk
- **Compat burden:** Old overloads everywhere (logger.cpp code size increases initially)
  - Mitigation: Aggressive cleanup phase post-Phase A once old code is understood
- **Test updates:** 8 of 11 test files need include updates
  - Mitigation: Scripted sed approach or manual for small repo size
- **Build blockers (LOGURU/GLOG):** Pre-existing per Phase 1 baseline
  - Mitigation: Attempt fixes in Phase B; document known issues if unfixable

### Low Risk
- **Macro changes:** Existing call sites unaffected (fmt-only, not printf)
- **API stability:** Deprecation paths preserved; no immediate breakage for transitional use

---

## References

- External review: (pasted at start of refactoring session)
- Old Phase 1 baseline: `docs/PHASE1_BASELINE_REPORT.md` (superseded)
- Old Phase 2 plan: `docs/PHASE2_IMPLEMENTATION_PLAN.md` (superseded)
- Plan details: `/Users/toufikbellaj/.claude/plans/lexical-crunching-lantern.md`
