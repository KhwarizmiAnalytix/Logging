## Summary

<!-- What does this change do, and why? -->

## Testing

<!-- Commands you ran, e.g.:
cmake -S . -B build -DLOGGING_ENABLE_COVERAGE=OFF && cmake --build build && ctest --test-dir build --output-on-failure
-->

- [ ] `ctest` passes locally (`LOGGING_BACKEND` used: __)
- [ ] Covered by an existing or new test in `Testing/Cxx/`
- [ ] Ran with a sanitizer (`LOGGING_ENABLE_SANITIZER=ON`) if touching memory/lifetime-sensitive code

## Checklist

- [ ] Updated `README.md` / doc comments if public behavior or CMake/Bazel options changed
- [ ] No new warnings introduced
