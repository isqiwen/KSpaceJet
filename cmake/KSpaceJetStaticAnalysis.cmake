include_guard()

function(ksj_add_linux_static_memory_leak_check_target)
  option(KSJ_ENABLE_STATIC_MEMORY_LEAK_CHECK "Enable the Linux-only Clang static analyzer memory leak check target."
         OFF)
  if(NOT KSJ_ENABLE_STATIC_MEMORY_LEAK_CHECK)
    return()
  endif()

  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
  endif()

  find_package(
    Python3
    COMPONENTS Interpreter
    QUIET)
  if(NOT Python3_Interpreter_FOUND)
    message(WARNING "Python3 was not found; ksj_static_memory_leak_check target will not be available.")
    return()
  endif()

  find_program(KSJ_CLANGXX_ANALYZER clang++)
  if(NOT KSJ_CLANGXX_ANALYZER)
    message(WARNING "clang++ was not found; ksj_static_memory_leak_check target will not be available.")
    return()
  endif()

  set(CMAKE_EXPORT_COMPILE_COMMANDS
      ON
      CACHE BOOL "Export compile commands for KSpaceJet static analysis." FORCE)
  set(KSJ_STATIC_MEMORY_LEAK_CHECK_JOBS
      "8"
      CACHE STRING "Parallel jobs for ksj_static_memory_leak_check.")
  set(KSJ_STATIC_MEMORY_LEAK_REPORT
      "${CMAKE_BINARY_DIR}/static_analysis/ksj_static_memory_leak_report.md"
      CACHE FILEPATH "Markdown report path for ksj_static_memory_leak_check.")

  add_custom_target(
    ksj_static_memory_leak_check
    COMMAND
      "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/ksj_static_analysis/check_memory_leaks.py"
      "--compile-commands=${CMAKE_BINARY_DIR}/compile_commands.json" "--project-root=${CMAKE_SOURCE_DIR}"
      "--clangxx=${KSJ_CLANGXX_ANALYZER}" "--jobs=${KSJ_STATIC_MEMORY_LEAK_CHECK_JOBS}"
      "--report=${KSJ_STATIC_MEMORY_LEAK_REPORT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Running Linux-only static memory leak analysis"
    VERBATIM)
endfunction()

function(ksj_add_numeric_dependency_boundary_check_target)
  find_package(
    Python3
    COMPONENTS Interpreter
    QUIET)
  if(NOT Python3_Interpreter_FOUND)
    message(WARNING "Python3 was not found; ksj_numeric_dependency_boundary_check target will not be available.")
    return()
  endif()

  option(KSJ_NUMERIC_DEPENDENCY_BOUNDARY_FAIL_ON_VIOLATION
         "Make ksj_numeric_dependency_boundary_check fail when dependency boundary violations are found." OFF)
  set(KSJ_NUMERIC_DEPENDENCY_BOUNDARY_REPORT
      "${CMAKE_BINARY_DIR}/static_analysis/ksj_numeric_dependency_boundary_report.md"
      CACHE FILEPATH "Markdown report path for ksj_numeric_dependency_boundary_check.")

  set(_ksj_numeric_boundary_fail_arg)
  if(KSJ_NUMERIC_DEPENDENCY_BOUNDARY_FAIL_ON_VIOLATION)
    list(APPEND _ksj_numeric_boundary_fail_arg "--fail-on-violation")
  endif()

  add_custom_target(
    ksj_numeric_dependency_boundary_check
    COMMAND
      "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/ksj_static_analysis/check_numeric_dependency_boundaries.py"
      "--project-root=${CMAKE_SOURCE_DIR}" "--report=${KSJ_NUMERIC_DEPENDENCY_BOUNDARY_REPORT}"
      ${_ksj_numeric_boundary_fail_arg}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Checking numeric dependency boundaries"
    VERBATIM)
endfunction()
