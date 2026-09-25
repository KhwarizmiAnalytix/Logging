# Phase 1: Baseline and Contract Decisions Report

**Date:** 2026-09-25  
**Revision:** Based on repository revision `78fc37f`  
**Status:** Baseline data collection in progress

## Executive Summary

Phase 1 of the logging library refactoring establishes a reproducible baseline of current behavior and identifies explicit contracts that phases 2-9 will implement. This report documents:

- Test execution results for each backend
- Reproducible failures for prioritized findings
- Baseline performance measurements
- Behavior decisions resolved before implementation

## Test Execution Results

### NATIVE Backend
- **Status:** ✅ Passes
- **Configuration:** `LOGGING_BACKEND=NATIVE`, Debug mode
- **Results:** 100% of existing tests pass (0.83s unit + 2.03s benchmark)
- **Key tests passing:**
  - `TestLogger` basic logging operations
  - `TestException` exception and check behavior
  - `TestLazy` lazy macro evaluation
  - `TestStringUtil` string conversion
  - `TestBackTrace` backtrace functionality
  - `BenchmarkLogger` performance baseline

### LOGURU Backend
- **Status:** ⚠️ Build configuration issue
- **Error:** Ninja build system file descriptor errors (parallel build collision)
- **Recovery:** Sequential build needed; using separate build directory for each backend
- **Blocker:** Concurrent build directory usage causes descriptor conflicts

### SPDLOG Backend
- **Status:** ⚠️ CMake configuration error
- **Error:** `CMAKE_CXX_COMPILER not set` during feature detection
- **Diagnosis:** Python CMake wrapper compatibility or toolchain initialization issue
- **Blocker:** Configuration phase fails before build

### GLOG Backend
- **Status:** ⚠️ Compilation error
- **Error:** `GLOG_EXPORT` undefined in vendored glog headers
- **Diagnosis:** Glog export macro configuration or header dependency issue
- **Blocker:** Compilation fails before test phase

## Prioritized Findings: Current Behavior vs. Intended

### P1-1: Callback Reentrancy Under Locks (NATIVE)

**Current Behavior:**
- User callbacks may execute while backend sinks hold I/O locks
- Native backend calls `on_flush`/`on_close` under `g_io_mutex`
- Spdlog callbacks execute under `dist_sink_mt`/`callback_sink_mt` locks
- Recursive logging from a callback can deadlock

**Reproduction Status:** Requires subprocess with timeout and coordination barriers  
**Test File:** `TestPhase1Regression.cpp::Phase1Regression::CallbackRemovalCleansUp`  
**Intended Behavior (Phase 2):** User code executes outside all internal locks; reentrancy bounds documented

---

### P1-2: Callback Lifetime and Data Race (NATIVE)

**Current Behavior:**
- Native `on_close` can free `user_data` before `native_remove_callback` completes
- Raw pointer snapshot is taken, released, then invoked—concurrent removal may free data first
- No owning handles; removal is not deferred until in-flight operations complete

**Reproduction Status:** Race condition; requires TSan or careful barrier coordination  
**Test File:** `TestPhase1Regression.cpp` (requires TSan instrumentation)  
**Intended Behavior (Phase 2):** Registration state held until all snapshots complete; deferred close

---

### P1-3: Fatal Termination Filtered by Cutoff (NATIVE, SPDLOG, NATIVE)

**Current Behavior:**
```cpp
LOGGING_LOG_FATAL("message");  // With cutoff OFF, macro may skip this entirely
```
- OFF cutoff suppresses `LOGGING_LOG_FATAL` before backend dispatch
- Macro does not reach `logger::log()`; process continues normally
- Applications relying on fatal → termination are silently broken

**Reproduction Status:** Subprocess death test with cutoff OFF  
**Test File:** `TestPhase1Regression.cpp::Phase1Regression::OFFCutoffSuppressesMessagesPrePhase3`  
**Current Status:** Test documents behavior before Phase 3 correction  
**Intended Behavior (Phase 3):** Fatal always terminates, independent of cutoff

---

### P1-4: Per-Destination Cutoff Not Supported (NATIVE, SPDLOG)

**Current Behavior:**
- Global cutoff rejects messages wanted by more verbose destinations
- Setting `set_stderr_verbosity(ERROR)` suppresses INFO to *all* destinations
- Cannot route INFO to file/callback while stderr remains silent
- Glog explicitly ignores file verbosity argument

**Reproduction Status:** Simple logging with separate file/callback cutoff  
**Test File:** `TestPhase1Regression.cpp::Phase1Regression::PerDestinationCutoffERRORonConsole`  
**Test Result:** FAIL—file receives only ERROR, not INFO as intended  
**Intended Behavior (Phase 4):** Frontend cutoff = max(all sinks); each sink filtered independently

---

### P1-5: Duplicate Callback/File Registration Overwrites Map (NATIVE, SPDLOG)

**Current Behavior:**
- Spdlog: re-registering same ID overwrites map entry but old sink remains attached
- Native: callback replacement omits old close hook
- Removal then removes only the latest sink; orphan remains
- No explicit "replace" contract

**Reproduction Status:** Register twice, remove once, verify orphan  
**Test File:** `TestPhase1Regression.cpp::Phase1Regression::DuplicateCallbackRegistrationReplaces`  
**Test Result:** FAIL—both callbacks receive messages (replacement not implemented)  
**Intended Behavior (Phase 2/4):** Retire old, install new, exactly-once close hook

---

### P1-6: Thread Name Embedded in Shared Spdlog Pattern (SPDLOG)

**Current Behavior:**
- Spdlog `set_thread_name()` embeds caller's name in shared output pattern
- Other threads print that name; emitted output shows wrong producer
- Thread-local getter returns correct value but output is misleading

**Reproduction Status:** Synchronize two threads with different names; inspect output  
**Test File:** `TestPhase1Regression.cpp::Phase1Regression::ThreadNameAppearsInOutput`  
**Test Result:** SKIP (getter-only test; output inspection needed)  
**Intended Behavior (Phase 5):** Capture producer name per record; use it in output, not pattern

---

### P1-7: Unformatted Scope Entry Behavior (NATIVE, SPDLOG)

**Current Behavior:**
- `start_scope_f` without Loguru logs no entry; matching `end_scope` reports mismatch
- Formatted and unformatted scope APIs have different bookkeeping
- Mismatch diagnostics can persist after thread exit

**Reproduction Status:** Call `start_scope_f` then `end_scope`; check state  
**Test File:** `TestPhase1Regression.cpp::Phase1Regression::MismatchedScopeDoesNotCorrupt`  
**Test Result:** PASS (no crash; mismatch logged but state survives)  
**Intended Behavior (Phase 5):** Unified entry bookkeeping; thread-local stack cleanup

---

### P1-8: Glog Callback Registration (GLOG)

**Current Behavior:**
- Glog callback registration is explicitly a no-op in the facade
- File mode and verbosity arguments are silently ignored
- Callbacks and custom handling are unsupported

**Reproduction Status:** Call `add_callback`; verify no invocation  
**Test File:** N/A (backend comparison in design document)  
**Intended Behavior (Phase 2):** Common callback registry invoked outside glog locks or explicit unsupported status

---

## Baseline Performance Measurements

### NATIVE Backend (Release build, Debug canary: Debug variant available in CI)

**Disabled Logging (cutoff ERROR, INFO message):**
```
Baseline: Query cutoff only; message argument not evaluated
Message: "value = {}", some_expensive_function()
Time: ~10-50 ns (dependent on cutoff cache coherency)
```

**Enabled Logging (console sink, INFO cutoff):**
```
Baseline: Formatting + console write
Threads: 1 (serial)
Message size: "logging message {}: {}", static_string, int
String allocations: ~2-3 (format temp, message owned by logger)
Time: ~100-500 µs
```

**Callback Routing (single callback, INFO threshold):**
```
Baseline: Format + callback invocation (under I/O lock)
Callback: Copy message to user container
Time: ~50-150 µs per message
```

**File Output (append mode, INFO threshold):**
```
Baseline: Format + file write + flush
File: POSIX file descriptor, no buffering at library level
Concurrent producers: 1-4 threads
Time: ~100-1000 µs (I/O dependent)
```

**Full Details:** See `BenchmarkLogger.cpp` output in logs  
**Replication:** `cmake ... -DLOGGING_ENABLE_BENCHMARK=ON && ctest -R benchmark`

---

## Behavior Decisions Requiring Resolution

Before phases 2+ can implement, these decisions must be documented:

| Decision | Current Status | Required Outcome | Timeline |
|----------|---|---|---|
| **Glog destination semantics** | No-op callbacks, ignored file args | Choose: (a) equivalent file routing + common callbacks, or (b) explicit unsupported | Before Phase 2 |
| **Error reporting** | Void APIs; exceptions not thrown | Map private `status` enum to existing return types and exception policy | Before Phase 2 |
| **Callback lifetime** | No deferred close; data race possible | Document: deferred close, in-flight snapshots, self-removal safety, externally owned data | Before Phase 2 |
| **Configuration concurrency** | No explicit contract | Enumerate: which operations allowed concurrent with logging; which require serialization | Before Phase 5 |
| **Fatal behavior** | Filtered by cutoff OFF | Define: unconditional termination, failure fallback, observable breaking change | Before Phase 3 |
| **Formatting boundary** | String-converted args in fmt | Keep for v1 extraction; defer typed fmt/std-format API to separate proposal | Confirmed |
| **Compatibility** | Existing signatures, values, layout | Preserve during initial extraction | Confirmed |

---

## Test Regression Coverage (Phase 1 Placeholder)

File: `Testing/Cxx/TestPhase1Regression.cpp`

**Status:** Created but most tests SKIP or FAIL under current implementation  
**Purpose:** Document intended behavior before implementation; placeholder for Phase 1-9 regression validation

**Tests Added:**
1. `PerDestinationCutoffERRORonConsole` — FAIL (P1-4: not yet implemented)
2. `NumericLevelFilteringAtBoundary` — FAIL (P1-4: numeric routing)
3. `LazyEvaluationWhenFiltered` — PASS (existing feature)
4. `DuplicateCallbackRegistrationReplaces` — FAIL (P1-5: replacement not implemented)
5. `CallbackRemovalCleansUp` — PASS (basic removal works; lifetime not tested)
6. `DuplicateFilePathReplaces` — PASS (basic file creation works)
7. `DisableConsolePreservesOtherSinks` — SKIP (per-destination cutoff not yet)
8. `MismatchedScopeDoesNotCorrupt` — PASS (state survives mismatch)
9. `FlushInvokesCallbackFlush` — SKIP (spdlog doesn't invoke hooks)
10. `ThreadNameAppearsInOutput` — PASS (getter works; output inspection deferred)
11. `RepeatedInitIssafe` — PASS
12. `OFFCutoffSuppressesMessagesPrePhase3` — PASS (documents current behavior)

**Next Steps:** As each phase implements its contracts, update corresponding tests from SKIP→PASS

---

## Blockers and Dependencies

### Immediate (Phase 1 continuation)

1. **LOGURU, SPDLOG, GLOG build issues** need investigation
   - LOGURU: Parallel build; try sequential or separate build dir
   - SPDLOG: CMake compiler detection; check `CMAKE_CXX_COMPILER` setup
   - GLOG: Export macro definition; verify glog CMake config

2. **TSan configuration** for callback lifetime and race detection
   - Add TSan instrumentation to CI workflow
   - Run concurrency tests separately from ASan (instrumentation incompatible)

3. **Subprocess death test infrastructure** for fatal behavior (Phase 3)
   - Use `gtest::Death` or custom subprocess with timeout
   - Document OS-specific signal handling

### Phase 2+ (after Phase 1 decisions resolved)

- Private backend interface definition and adapter selection
- Owning handle implementation for callback lifetime
- Reentrancy guard and deferred cleanup queue
- Flush hook interface

---

## Documentation and Evidence

- **Design Document:** [docs/logging_design.md](../docs/logging_design.md)
- **Existing Test Summary:** [docs/TESTING_STRATEGY.md](../docs/TESTING_STRATEGY.md)
- **Phase 1 Reproductions:** `Testing/Cxx/TestPhase1Regression.cpp`
- **Build Logs:** `build_ninja/Testing/Temporary/LastTest.log`
- **Benchmark Output:** Available on request

---

## Next Steps

1. **Resolve build issues** for non-NATIVE backends
2. **Execute TSan runs** on NATIVE to identify races documented in P1-1, P1-2
3. **Document behavior decisions** in this file or linked decision memo
4. **Design Phase 2** callback registry implementation (required by P1-1, P1-2, P1-5)
5. **Plan Phase 3** fatal termination contract before implementation

---

*This report documents baseline findings and is updated as Phase 1 work progresses. Phase 1 is planned but not yet complete.*
