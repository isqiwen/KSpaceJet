if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR "Configure KSpaceJet tests from the repository root CMakeLists.txt.")
endif()

function(ksj_register_test_suite_executable suite_name target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "ksj_register_test_suite_executable() requires an existing target: ${target_name}")
  endif()

  set_property(GLOBAL APPEND PROPERTY KSJ_${suite_name}_EXECUTABLE_TARGETS ${target_name})
endfunction()

function(ksj_append_runner_metadata_var output_var variable_name)
  if(DEFINED ${variable_name})
    string(APPEND ${output_var} "${variable_name}=${${variable_name}}\n")
    set(${output_var}
        "${${output_var}}"
        PARENT_SCOPE)
  endif()
endfunction()

function(ksj_generate_test_suite_runner suite_name script_name)
  # The detailed environment-report runner is a POSIX shell program. Windows uses the ordinary CTest registrations
  # created by ksj_add_gtest() instead; that keeps the test binaries and their discovery identical on both platforms
  # without installing a nonfunctional .sh launcher on Windows.
  if(WIN32)
    return()
  endif()

  get_property(_ksj_runner_targets GLOBAL PROPERTY KSJ_${suite_name}_EXECUTABLE_TARGETS)
  if(NOT _ksj_runner_targets)
    return()
  endif()

  string(TOLOWER "${suite_name}" _ksj_runner_suite_lower)
  set(_ksj_runner_context_file "ksj_${_ksj_runner_suite_lower}_run_context.txt")

  set(_ksj_git_commit "unknown")
  set(_ksj_git_branch "unknown")
  set(_ksj_git_describe "unknown")
  set(_ksj_git_status "unknown")
  if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
      COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse --verify HEAD
      OUTPUT_VARIABLE _ksj_git_commit
      ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
      COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse --abbrev-ref HEAD
      OUTPUT_VARIABLE _ksj_git_branch
      ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
      COMMAND git -C "${CMAKE_SOURCE_DIR}" describe --always --dirty --tags
      OUTPUT_VARIABLE _ksj_git_describe
      ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
      COMMAND git -C "${CMAKE_SOURCE_DIR}" status --short
      OUTPUT_VARIABLE _ksj_git_status_output
      ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_ksj_git_status_output STREQUAL "")
      set(_ksj_git_status "clean")
    else()
      set(_ksj_git_status "dirty")
    endif()
  endif()

  set(_ksj_runner_build_metadata "")
  string(APPEND _ksj_runner_build_metadata "suite=${suite_name}\n")
  string(APPEND _ksj_runner_build_metadata "project_name=${PROJECT_NAME}\n")
  string(APPEND _ksj_runner_build_metadata "project_version=${PROJECT_VERSION}\n")
  string(APPEND _ksj_runner_build_metadata "source_dir=${CMAKE_SOURCE_DIR}\n")
  string(APPEND _ksj_runner_build_metadata "build_dir=${CMAKE_BINARY_DIR}\n")
  string(APPEND _ksj_runner_build_metadata "install_prefix=${CMAKE_INSTALL_PREFIX}\n")
  string(APPEND _ksj_runner_build_metadata "cmake_version=${CMAKE_VERSION}\n")
  string(APPEND _ksj_runner_build_metadata "cmake_generator=${CMAKE_GENERATOR}\n")
  string(APPEND _ksj_runner_build_metadata "cmake_build_type=${CMAKE_BUILD_TYPE}\n")
  string(APPEND _ksj_runner_build_metadata "cxx_compiler_id=${CMAKE_CXX_COMPILER_ID}\n")
  string(APPEND _ksj_runner_build_metadata "cxx_compiler_version=${CMAKE_CXX_COMPILER_VERSION}\n")
  string(APPEND _ksj_runner_build_metadata "cxx_compiler=${CMAKE_CXX_COMPILER}\n")
  string(APPEND _ksj_runner_build_metadata "cxx_flags=${CMAKE_CXX_FLAGS}\n")
  string(APPEND _ksj_runner_build_metadata "cxx_release_flags=${CMAKE_CXX_FLAGS_RELEASE}\n")
  string(APPEND _ksj_runner_build_metadata "git_commit=${_ksj_git_commit}\n")
  string(APPEND _ksj_runner_build_metadata "git_branch=${_ksj_git_branch}\n")
  string(APPEND _ksj_runner_build_metadata "git_describe=${_ksj_git_describe}\n")
  string(APPEND _ksj_runner_build_metadata "git_status_at_configure=${_ksj_git_status}\n")
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata KSJ_DEPENDENCY_PROVIDER)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata CMAKE_TOOLCHAIN_FILE)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata Eigen3_VERSION)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata Eigen3_DIR)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata Intel_VERSION)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata Intel_DIR)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata OpenCV_VERSION)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata OpenCV_DIR)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata Boost_VERSION)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata Boost_DIR)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata GTest_VERSION)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata GTest_DIR)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata matio_VERSION)
  ksj_append_runner_metadata_var(_ksj_runner_build_metadata matio_DIR)

  set(_ksj_runner_conan_generator_paths "")
  foreach(_ksj_conan_generator_path IN LISTS CMAKE_PREFIX_PATH)
    string(APPEND _ksj_runner_conan_generator_paths "  ${_ksj_conan_generator_path}\n")
  endforeach()

  set(_ksj_runner_dependency_version_lines "")
  set(_ksj_runner_dependency_candidate_paths ${CMAKE_PREFIX_PATH})
  list(REMOVE_DUPLICATES _ksj_runner_dependency_candidate_paths)
  foreach(_ksj_dependency_candidate_path IN LISTS _ksj_runner_dependency_candidate_paths)
    set(_ksj_dependency_prefix "${_ksj_dependency_candidate_path}")
    get_filename_component(_ksj_dependency_leaf "${_ksj_dependency_prefix}" NAME)
    if(_ksj_dependency_leaf MATCHES "^(bin|include|lib|lib64|share)$")
      get_filename_component(_ksj_dependency_prefix "${_ksj_dependency_prefix}" DIRECTORY)
    endif()

    get_filename_component(_ksj_dependency_version "${_ksj_dependency_prefix}" NAME)
    get_filename_component(_ksj_dependency_parent "${_ksj_dependency_prefix}" DIRECTORY)
    get_filename_component(_ksj_dependency_name "${_ksj_dependency_parent}" NAME)
    if(_ksj_dependency_name AND _ksj_dependency_version)
      list(APPEND _ksj_runner_dependency_version_lines
           "${_ksj_dependency_name}=${_ksj_dependency_version} (${_ksj_dependency_prefix})")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _ksj_runner_dependency_version_lines)
  list(SORT _ksj_runner_dependency_version_lines)
  set(_ksj_runner_dependency_versions "")
  foreach(_ksj_dependency_version_line IN LISTS _ksj_runner_dependency_version_lines)
    string(APPEND _ksj_runner_dependency_versions "${_ksj_dependency_version_line}\n")
  endforeach()

  set(_ksj_runner_path "${CMAKE_BINARY_DIR}/bin/${script_name}")
  set(_ksj_runner_content
      [=[#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
REPORT_DIR="${SCRIPT_DIR}/reports"
RUN_CONTEXT_FILE="${REPORT_DIR}/]=])
  string(APPEND _ksj_runner_content "${_ksj_runner_context_file}")
  string(
    APPEND
    _ksj_runner_content
    [=["

write_run_context() {
  mkdir -p "${REPORT_DIR}"
  {
    cat <<'KSJ_RUN_CONTEXT_HEADER'
KSpaceJet test suite run context

[build]
]=])
  string(APPEND _ksj_runner_content "${_ksj_runner_build_metadata}")
  string(
    APPEND
    _ksj_runner_content
    [=[

[conan_generator_paths]
]=])
  string(APPEND _ksj_runner_content "${_ksj_runner_conan_generator_paths}")
  string(
    APPEND
    _ksj_runner_content
    [=[

[dependency_versions]
]=])
  string(APPEND _ksj_runner_content "${_ksj_runner_dependency_versions}")
  string(
    APPEND
    _ksj_runner_content
    [=[
KSJ_RUN_CONTEXT_HEADER

    printf '\n[run]\n'
    printf 'generated_at_utc=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf 'script=%s\n' "$0"
    printf 'script_dir=%s\n' "${SCRIPT_DIR}"
    printf 'working_dir=%s\n' "$(pwd)"
    printf 'arguments='
    if [ "$#" -eq 0 ]; then
      printf '(none)\n'
    else
      printf '\n'
      for arg in "$@"; do
        printf '  %s\n' "${arg}"
      done
    fi

    printf '\n[hardware]\n'
    if command -v hostname >/dev/null 2>&1; then
      printf 'hostname=%s\n' "$(hostname)"
    fi
    if command -v uname >/dev/null 2>&1; then
      printf 'uname=%s\n' "$(uname -a)"
    fi
    if command -v nproc >/dev/null 2>&1; then
      printf 'nproc=%s\n' "$(nproc)"
    fi
    if command -v getconf >/dev/null 2>&1; then
      printf 'page_size=%s\n' "$(getconf PAGE_SIZE 2>/dev/null || true)"
    fi
    if command -v lscpu >/dev/null 2>&1; then
      printf '\n[lscpu]\n'
      lscpu || true
    elif [ -r /proc/cpuinfo ]; then
      printf '\n[/proc/cpuinfo]\n'
      cat /proc/cpuinfo || true
    fi
    if command -v numactl >/dev/null 2>&1; then
      printf '\n[numactl --hardware]\n'
      numactl --hardware || true
    fi
    if command -v free >/dev/null 2>&1; then
      printf '\n[free -h]\n'
      free -h || true
    elif [ -r /proc/meminfo ]; then
      printf '\n[/proc/meminfo]\n'
      cat /proc/meminfo || true
    fi

    printf '\n[runtime_environment]\n'
    env | LC_ALL=C sort | grep -E '^(KSJ_|OMP_|KMP_|MKL_|LD_LIBRARY_PATH=|PATH=)' || true

    printf '\n[runtime_linked_libraries]\n'
]=])

  foreach(_ksj_runner_target IN LISTS _ksj_runner_targets)
    string(APPEND _ksj_runner_content "    log_linked_libraries \"${_ksj_runner_target}\"\n")
  endforeach()

  string(
    APPEND
    _ksj_runner_content
    [=[
  } >"${RUN_CONTEXT_FILE}"
  printf 'run context: %s\n' "${RUN_CONTEXT_FILE}"
}

log_linked_libraries() {
  name=$1
  exe="${SCRIPT_DIR}/${name}"
  printf '\n== %s ==\n' "${name}"
  if [ ! -x "${exe}" ]; then
    printf 'missing executable: %s\n' "${exe}"
    return
  fi
  if command -v ldd >/dev/null 2>&1; then
    ldd "${exe}" || true
  else
    printf 'ldd not found\n'
  fi
}

run_executable() {
  name=$1
  shift
  exe="${SCRIPT_DIR}/${name}"
  if [ ! -x "${exe}" ]; then
    printf '%s\n' "missing executable: ${exe}" >&2
    exit 127
  fi
  printf '\n==> %s\n' "${name}"
  "${exe}" "$@"
}

]=])

  string(APPEND _ksj_runner_content "write_run_context \"\$@\"\n")

  foreach(_ksj_runner_target IN LISTS _ksj_runner_targets)
    if(NOT TARGET ${_ksj_runner_target})
      message(FATAL_ERROR "Registered ${suite_name} runner target no longer exists: ${_ksj_runner_target}")
    endif()
    string(APPEND _ksj_runner_content "run_executable \"${_ksj_runner_target}\" \"\$@\"\n")
  endforeach()

  file(
    GENERATE
    OUTPUT "${_ksj_runner_path}"
    CONTENT "${_ksj_runner_content}"
            FILE_PERMISSIONS
            OWNER_READ
            OWNER_WRITE
            OWNER_EXECUTE
            GROUP_READ
            GROUP_EXECUTE
            WORLD_READ
            WORLD_EXECUTE)

  if(KSJ_ENABLE_INSTALL_RULES)
    install(PROGRAMS "${_ksj_runner_path}" DESTINATION "${KSJ_INSTALL_BINDIR}")
  endif()
endfunction()

if(KSJ_BUILD_UNIT_TESTS)
  include(CMakeParseArguments)

  find_package(GTest CONFIG REQUIRED)

  function(ksj_add_gtest target_name)
    set(options)
    set(oneValueArgs MODULE)
    set(multiValueArgs SOURCES LINK_LIBS INCLUDE_DIRS LABELS)
    cmake_parse_arguments(KSJ_TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT KSJ_TEST_MODULE)
      message(FATAL_ERROR "ksj_add_gtest(${target_name}) requires MODULE.")
    endif()
    if(NOT KSJ_TEST_SOURCES)
      message(FATAL_ERROR "ksj_add_gtest(${target_name}) requires SOURCES.")
    endif()

    add_executable(${target_name} ${KSJ_TEST_SOURCES})
    ksj_target_enable_defaults(${target_name})
    target_include_directories(${target_name} PRIVATE ${KSJ_TEST_INCLUDE_DIRS})
    target_link_libraries(${target_name} PRIVATE GTest::gtest_main ${KSJ_TEST_LINK_LIBS})
    ksj_set_executable_runtime_path(${target_name})
    ksj_install_target(${target_name})
    ksj_register_test_suite_executable(UNIT_TESTS ${target_name})

    project_kit_add_test(
      NAME
      ${KSJ_TEST_MODULE}.${target_name}
      TARGET
      ${target_name}
      LABELS
      unit
      ${KSJ_TEST_LABELS})
  endfunction()

  add_subdirectory(unit)
  ksj_generate_test_suite_runner(UNIT_TESTS run_ksj_unit_tests.sh)
endif()

if(KSJ_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
  ksj_generate_test_suite_runner(BENCHMARKS run_ksj_benchmarks.sh)
endif()

if(KSJ_BUILD_RESEARCH)
  add_subdirectory(research)
  ksj_generate_test_suite_runner(RESEARCH run_ksj_research.sh)
endif()
