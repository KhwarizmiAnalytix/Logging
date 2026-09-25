# Phase 2: Callback Lifetime and Reentrancy - Implementation Plan

**Focus:** Address the three P1 (Priority 1) critical issues from the architecture review

## Critical Issues to Fix

### P1-1: Callback Reentrancy Deadlock
**Problem:** User callbacks execute while backend I/O locks are held  
**Impact:** Logging from within a callback can deadlock  
**Current locations:**
- Native: `on_flush`/`on_close` run under `g_io_mutex` (logger.cpp)
- Spdlog: callbacks run under `dist_sink_mt`/`callback_sink_mt` locks (vendored spdlog)

**Required fix:** Callbacks must execute outside all internal locks

### P1-2: Callback Lifetime Data Race  
**Problem:** Native backend can free `user_data` before `remove_callback()` completes  
**Impact:** Use-after-free crash when callbacks are removed during concurrent logging  
**Current issue:** Raw pointer snapshot taken, released, invoked—concurrent removal frees data first  

**Required fix:** Registration state must remain alive until all in-flight operations complete

### P1-3: Fatal Suppression Bug
**Problem:** `LOGGING_LOG_FATAL` filtered out by OFF cutoff before backend dispatch  
**Impact:** Process continues when it should terminate  
**Current issue:** Macro filter suppresses fatal before reaching logger::log()  

**Required fix:** Fatal termination independent of ordinary cutoff logic

---

## Implementation Strategy

### Phase 2a: Common Registry with Owning Handles

**Objective:** Replace raw-pointer callbacks with lifetime-managed registration state

**Key design decisions:**
1. Create private `callback_registry` class to hold registration state
2. Use owning handle (`shared_ptr`/`unique_ptr`) for registration entries
3. Snapshot handles under mutex; invoke outside locks
4. Defer close hooks until last snapshot is released

**Files to create:**
- `src/detail/callback_registry.h` (private header)
- `src/detail/callback_registry.cpp` (private implementation)

**Files to modify:**
- `include/logger/logger.cpp` - replace direct callback dispatch with registry calls
- Build system configuration (CMake/Bazel) - add new source files

**Key responsibilities:**
```
callback_registry:
  - Owns registration entries (ID → state mapping)
  - Provides snapshot() for log operations
  - Manages deferred close execution
  - Enforces reentrancy guard (no nested callback dispatch)
  - Thread-safe under internal mutex
```

**Public API (unchanged):**
```cpp
logger::add_callback(id, handler, user_data, level)    // Existing signature
logger::remove_callback(id)                              // Existing signature
logger::flush()                                           // Existing signature
```

**Private interface:**
```cpp
namespace logging::detail {
  struct registration_snapshot {
    handler_func handler;
    void* user_data;
    flush_hook_func flush_hook;  // if supported
    close_hook_func close_hook;   // if supported
  };

  class callback_registry {
  public:
    callback_registry();
    ~callback_registry();

    void add(string_view id, registration_snapshot entry);
    void remove(string_view id);  // Marks for removal; defer cleanup
    vector<registration_snapshot> snapshot();

    void flush_hook_calls();
    void close_hook_calls();  // Deferred until all snapshots released
  };
}
```

---

### Phase 2b: Reentrancy Guard

**Objective:** Prevent recursive callback dispatch while allowing nested logging to reach output sinks

**Mechanism:** Thread-local flag that blocks callback invocation during current callback execution

**Implementation:**
```cpp
thread_local bool in_user_callback = false;

void dispatch_callbacks(const record& rec) {
  if (in_user_callback) {
    // Nested log from callback: output to sinks, but skip callbacks
    write_to_sinks(rec);
    return;
  }

  guard guard_reentrancy(in_user_callback, true);  // RAII set/restore
  auto snapshots = registry.snapshot();
  for (auto& snapshot : snapshots) {
    snapshot.handler(snapshot.user_data, rec);
  }
}
```

**Behavior:**
- First callback: `in_user_callback = true`, invoke handler
- Logging from within handler: reaches sinks, but handler is not invoked again
- Handler returns: `in_user_callback = false`

---

### Phase 2c: Fatal Termination Fix (Phase 3 prerequisite)

**Objective:** Make fatal termination independent of ordinary cutoff logic

**Current code pattern (logger.h):**
```cpp
#define LOGGING_LOG_FATAL(fmt, ...) \
  if (logger::get_current_verbosity_cutoff() <= logger_verbosity_enum::VERBOSITY_FATAL) { \
    logger::log(...);  // Can be suppressed by OFF cutoff
  }
```

**Problem:** OFF cutoff causes entire macro to skip

**Required change:** Fatal path must bypass ordinary cutoff checks
```cpp
#define LOGGING_LOG_FATAL(fmt, ...) \
  logger::log_fatal(...)  // Always calls backend, independent of cutoff
```

**Backend responsibility:**
- `log_fatal()` always terminates, even if cutoff is OFF
- Attempts output; failures don't prevent termination
- Calls flush hooks and attempts callback delivery as best effort
- Terminates with `std::abort()` or platform-specific termination

---

## Implementation Phases

### Phase 2 Deliverables

| Component | Task | Dependencies | Owner |
|-----------|------|---|---|
| **Common Registry** | Define private callback_registry interface | None | Phase 2a |
| | Implement owning handles and snapshot semantics | Registry interface | Phase 2a |
| | Integrate with logger.cpp dispatch | Registry impl | Phase 2a |
| **Reentrancy Guard** | Add thread-local guard mechanism | Registry impl | Phase 2b |
| | Test nested logging from callbacks | Reentrancy guard | Phase 2b |
| **Testing** | Callback removal during concurrent logging | Owning handles | Ongoing |
| | Nested callback re-entrance behavior | Reentrancy guard | Ongoing |
| | No deadlock under load | All Phase 2 | Ongoing |

### Phase 3 Prerequisite: Fatal Termination

- Modify `LOGGING_LOG_FATAL` macro in `logger.h`
- Add `logger::log_fatal()` function in `logger.cpp`
- Implement backend-specific termination (each adapter)
- Test with subprocess death tests

---

## Critical Code Locations

### Current Implementation to Modify

**logger.cpp:**
- Lines ~200-250: Current callback dispatch (spdlog path)
- Lines ~300-350: Current callback dispatch (native path)
- Lines ~400-450: Fatal message dispatch

**logger.h:**
- LOGGING_LOG_FATAL macro definition
- LOGGING_LOG_* macros (verify they compile to same dispatch)

**Native backend (logger.cpp #if LOGGING_HAS_NATIVE):**
- `g_io_mutex` usage around I/O operations
- `g_callbacks` map for storing callbacks
- `native_remove_callback()` implementation

**Spdlog backend (logger.cpp #if LOGGING_HAS_SPDLOG):**
- Callback sink integration
- Distribution sink callback invocation

---

## Acceptance Criteria for Phase 2

1. **No deadlock:** Logging from callback completes without hanging
2. **No use-after-free:** Removing callback during concurrent logging is safe
3. **Bounded recursion:** Nested callbacks reach sinks but don't invoke handlers
4. **Exactly-once close:** Deferred hooks execute once after all snapshots release
5. **Backward compatible:** Public API signatures unchanged; existing code compiles

---

## Testing Strategy

### Unit Tests (existing TestLogger.cpp + new TestPhase2.cpp)

1. **Callback reentrancy:**
   - Register callback that logs (enabled)
   - Verify outer callback received one message
   - Verify nested log reached output sinks
   - Verify inner callback NOT invoked

2. **Concurrent removal:**
   - Thread A: Logs in tight loop
   - Thread B: Adds callback, waits, removes callback
   - Thread C: Adds callback, waits, removes callback
   - Verify no use-after-free (run under ASan/TSan)
   - Verify each callback saw only its own registrations

3. **Deferred close:**
   - Track close hook calls
   - Remove callback during concurrent logging
   - Verify close called exactly once
   - Verify called only after last log completes

### Integration Tests (subprocesses with timeouts)

1. **Deadlock detection:** Timeout test with callback that logs
2. **Race condition detection:** TSan instrumentation on concurrent scenarios

---

## Risk Assessment

### Medium Risk: Reentrancy Guard

- **Risk:** Nested logs could silently drop if guard incorrectly detects recursive case
- **Mitigation:** Clear logging of dispatch guard state; test with nested scopes
- **Fallback:** Recursive mutex if guard approach fails

### Medium Risk: Deferred Close

- **Risk:** Leaked memory if snapshot reference counting has bug
- **Mitigation:** Manual inspection of snapshot lifetime; valgrind/asan testing
- **Fallback:** Synchronous close if deferred approach causes issues

### Low Risk: API Compatibility

- **Risk:** Public signature changes could break downstream
- **Mitigation:** Preserve existing add_callback/remove_callback signatures
- **Fallback:** Wrapper functions if internal interface needs to differ

---

## Success Metrics

1. ✅ All existing tests continue to pass
2. ✅ New reentrancy tests pass (no deadlock)
3. ✅ Concurrent removal tests pass (no use-after-free)
4. ✅ ASan/TSan runs show no new issues
5. ✅ Phase 2 regression tests (when created) pass

---

## Next Steps

1. **Code review:** Verify callback_registry design before implementation
2. **Create Phase 2 test file:** Focused tests for P1-1 and P1-2
3. **Implement callback_registry:** Private owning handles
4. **Integrate with logger.cpp:** Replace callback dispatch paths
5. **Run full test suite:** Verify no regressions
6. **Begin Phase 3:** Fatal termination fix (after Phase 2 callbacks are solid)
