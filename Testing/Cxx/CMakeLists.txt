# =============================================================================
# Logging Library C++
# Tests
# =============================================================================
include(LoggingDependencies)

logging_setup_gtest()

file(GLOB_RECURSE test_sources LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/Test*.cpp")

set(TestFiles)
foreach(_test_source IN ITEMS ${test_sources})
  get_filename_component(file_name "${_test_source}" NAME)
  if(file_name MATCHES "TestLoggerDisableSignalHandler")
    continue()
  endif()
  list(APPEND TestFiles "${_test_source}")
endforeach()

add_executable(LoggingCxxTests ${TestFiles})

# Testing/ (sources + generated *Test.h) and third-party includes match the Logging::Logging
# interface.
target_include_directories(
  LoggingCxxTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/.."
)
if(LOGGING_DEPENDENCY_INCLUDE_DIRS)
  target_include_directories(LoggingCxxTests PRIVATE ${LOGGING_DEPENDENCY_INCLUDE_DIRS})
endif()

target_compile_definitions(
  LoggingCxxTests PRIVATE LOGGING_HAS_GTEST=1 LOGGING_GOOGLE_TEST USE_GTEST _VARIADIC_MAX=10
)

target_link_libraries(LoggingCxxTests PRIVATE Gtest::gtest Gtest::gtest_main Logging::Logging)
if(LOGGING_DEPENDENCY_LIBS)
  target_link_libraries(LoggingCxxTests PRIVATE ${LOGGING_DEPENDENCY_LIBS})
endif()

set_target_properties(
  LoggingCxxTests PROPERTIES FOLDER "Tests" VS_DEBUGGER_WORKING_DIRECTORY
                                            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

add_test(NAME LoggingCxxTests COMMAND LoggingCxxTests
         WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

set_tests_properties(LoggingCxxTests PROPERTIES TIMEOUT 120 LABELS "Logging;Unit")

if(COMMAND logging_set_target_output_directories)
  logging_set_target_output_directories(LoggingCxxTests)
endif()

if(LOGGING_ENABLE_BENCHMARK)
  logging_setup_benchmark()
  _create_third_party_interface_targets(
    "Benchmark::benchmark=benchmark::benchmark|benchmark"
    "Benchmark::benchmark_main=benchmark::benchmark_main|benchmark_main"
  )
  if(NOT TARGET Benchmark::benchmark)
    message(FATAL_ERROR "Google Benchmark target unavailable after logging_setup_benchmark()")
  endif()

  file(GLOB_RECURSE bench_sources LIST_DIRECTORIES false
       "${CMAKE_CURRENT_SOURCE_DIR}/Benchmark*.cpp"
  )

  foreach(_bench_source IN ITEMS ${bench_sources})
    get_filename_component(bench_name "${_bench_source}" NAME_WE)
    string(REPLACE "Benchmark" "" bench_suffix "${bench_name}")
    string(TOLOWER "${bench_suffix}" bench_suffix_lower)
    set(target_name "benchmark_logging_${bench_suffix_lower}")

    add_executable(${target_name} "${_bench_source}")
    target_include_directories(
      ${target_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/.."
    )
    if(LOGGING_DEPENDENCY_INCLUDE_DIRS)
      target_include_directories(${target_name} PRIVATE ${LOGGING_DEPENDENCY_INCLUDE_DIRS})
    endif()
    target_link_libraries(
      ${target_name} PRIVATE Benchmark::benchmark Benchmark::benchmark_main Logging::Logging
    )
    if(LOGGING_DEPENDENCY_LIBS)
      target_link_libraries(${target_name} PRIVATE ${LOGGING_DEPENDENCY_LIBS})
    endif()
    if(COMMAND logging_set_target_output_directories)
      logging_set_target_output_directories(${target_name})
    endif()

    add_test(NAME ${target_name} COMMAND ${target_name} --benchmark_min_time=0.01s
             WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
    set_tests_properties(
      ${target_name} PROPERTIES TIMEOUT 600 LABELS "Benchmark;Performance;Logging"
    )
  endforeach()
endif()
