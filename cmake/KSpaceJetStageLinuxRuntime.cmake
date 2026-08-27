# Stages the Conan/Intel-owned ELF closure needed to launch a build-tree target without activating conanrun.sh. This
# script intentionally runs after link, because file(GET_RUNTIME_DEPENDENCIES) operates on actual ELF files.

if(NOT DEFINED KSJ_RUNTIME_OUTPUT_DIR OR KSJ_RUNTIME_OUTPUT_DIR STREQUAL "")
  message(FATAL_ERROR "KSJ_RUNTIME_OUTPUT_DIR is required.")
endif()
if(NOT DEFINED KSJ_RUNTIME_DIRECTORIES OR KSJ_RUNTIME_DIRECTORIES STREQUAL "")
  message(FATAL_ERROR "KSJ_RUNTIME_DIRECTORIES is required.")
endif()

string(REPLACE "|" ";" _ksj_runtime_directories "${KSJ_RUNTIME_DIRECTORIES}")
set(_ksj_runtime_source_roots)
foreach(_ksj_runtime_directory IN LISTS _ksj_runtime_directories)
  if(IS_DIRECTORY "${_ksj_runtime_directory}")
    file(REAL_PATH "${_ksj_runtime_directory}" _ksj_runtime_directory_real)
    list(APPEND _ksj_runtime_source_roots "${_ksj_runtime_directory_real}")
  endif()
endforeach()
if(NOT _ksj_runtime_source_roots)
  message(FATAL_ERROR "KSJ_RUNTIME_DIRECTORIES contains no existing runtime directory.")
endif()
list(REMOVE_DUPLICATES _ksj_runtime_source_roots)

set(_ksj_runtime_modules)
if(DEFINED KSJ_RUNTIME_MODULES AND NOT KSJ_RUNTIME_MODULES STREQUAL "")
  string(REPLACE "|" ";" _ksj_runtime_modules "${KSJ_RUNTIME_MODULES}")
endif()

set(_ksj_get_runtime_dependency_args)
if(DEFINED KSJ_RUNTIME_EXECUTABLE AND NOT KSJ_RUNTIME_EXECUTABLE STREQUAL "")
  if(NOT EXISTS "${KSJ_RUNTIME_EXECUTABLE}")
    message(FATAL_ERROR "Runtime executable does not exist: ${KSJ_RUNTIME_EXECUTABLE}")
  endif()
  list(APPEND _ksj_get_runtime_dependency_args EXECUTABLES "${KSJ_RUNTIME_EXECUTABLE}")
endif()
foreach(_ksj_runtime_module IN LISTS _ksj_runtime_modules)
  if(NOT EXISTS "${_ksj_runtime_module}")
    message(FATAL_ERROR "Runtime module does not exist: ${_ksj_runtime_module}")
  endif()
endforeach()
if(_ksj_runtime_modules)
  list(APPEND _ksj_get_runtime_dependency_args MODULES ${_ksj_runtime_modules})
endif()
if(NOT _ksj_get_runtime_dependency_args)
  message(FATAL_ERROR "A runtime executable or module is required for dependency staging.")
endif()
list(APPEND _ksj_get_runtime_dependency_args DIRECTORIES ${_ksj_runtime_source_roots})

file(
  GET_RUNTIME_DEPENDENCIES
  RESOLVED_DEPENDENCIES_VAR
  _ksj_resolved_dependencies
  UNRESOLVED_DEPENDENCIES_VAR
  _ksj_unresolved_dependencies
  CONFLICTING_DEPENDENCIES_PREFIX
  _ksj_conflicting_dependencies
  ${_ksj_get_runtime_dependency_args}
  PRE_EXCLUDE_REGEXES
  "linux-vdso\\.so.*"
  POST_EXCLUDE_REGEXES
  "^/lib/.*"
  "^/lib64/.*"
  "^/usr/lib/.*"
  "^/usr/lib64/.*")

if(_ksj_unresolved_dependencies)
  list(JOIN _ksj_unresolved_dependencies ", " _ksj_unresolved_dependencies_text)
  message(FATAL_ERROR "Unresolved Linux runtime dependencies: ${_ksj_unresolved_dependencies_text}")
endif()

file(MAKE_DIRECTORY "${KSJ_RUNTIME_OUTPUT_DIR}")
file(REAL_PATH "${KSJ_RUNTIME_OUTPUT_DIR}" _ksj_runtime_output_dir_real)
set(_ksj_conflicting_dependencies_to_copy)
foreach(_ksj_conflicting_filename IN LISTS _ksj_conflicting_dependencies_FILENAMES)
  set(_ksj_conflicting_paths "${_ksj_conflicting_dependencies_${_ksj_conflicting_filename}}")
  set(_ksj_conflicting_source_candidates)
  set(_ksj_conflicting_source_hash)
  foreach(_ksj_conflicting_path IN LISTS _ksj_conflicting_paths)
    if(NOT EXISTS "${_ksj_conflicting_path}")
      continue()
    endif()

    file(REAL_PATH "${_ksj_conflicting_path}" _ksj_conflicting_path_real)
    string(FIND "${_ksj_conflicting_path_real}" "${_ksj_runtime_output_dir_real}/" _ksj_conflicting_in_output)
    if(_ksj_conflicting_in_output EQUAL 0)
      continue()
    endif()

    set(_ksj_conflicting_is_owned FALSE)
    foreach(_ksj_runtime_source_root IN LISTS _ksj_runtime_source_roots)
      string(FIND "${_ksj_conflicting_path_real}" "${_ksj_runtime_source_root}/" _ksj_conflicting_in_source_root)
      if(_ksj_conflicting_in_source_root EQUAL 0)
        set(_ksj_conflicting_is_owned TRUE)
        break()
      endif()
    endforeach()
    if(NOT _ksj_conflicting_is_owned)
      message(FATAL_ERROR "Conflicting runtime dependency is outside the approved Conan/Intel roots: "
                          "${_ksj_conflicting_path}")
    endif()

    file(SHA256 "${_ksj_conflicting_path}" _ksj_conflicting_candidate_hash)
    if(_ksj_conflicting_source_hash AND NOT _ksj_conflicting_source_hash STREQUAL _ksj_conflicting_candidate_hash)
      message(FATAL_ERROR "Conflicting Conan/Intel runtime sources for ${_ksj_conflicting_filename}: "
                          "${_ksj_conflicting_paths}")
    endif()
    set(_ksj_conflicting_source_hash "${_ksj_conflicting_candidate_hash}")
    list(APPEND _ksj_conflicting_source_candidates "${_ksj_conflicting_path}")
  endforeach()
  if(NOT _ksj_conflicting_source_candidates)
    message(FATAL_ERROR "No Conan/Intel source is available for ${_ksj_conflicting_filename}.")
  endif()
  list(REMOVE_DUPLICATES _ksj_conflicting_source_candidates)
  list(GET _ksj_conflicting_source_candidates 0 _ksj_conflicting_source)
  list(APPEND _ksj_conflicting_dependencies_to_copy "${_ksj_conflicting_source}")
endforeach()

set(_ksj_runtime_dependencies_to_copy)
set(_ksj_unowned_runtime_dependencies)
foreach(_ksj_dependency IN LISTS _ksj_resolved_dependencies)
  if(NOT EXISTS "${_ksj_dependency}")
    message(FATAL_ERROR "Resolved runtime dependency does not exist: ${_ksj_dependency}")
  endif()

  file(REAL_PATH "${_ksj_dependency}" _ksj_dependency_real)
  string(FIND "${_ksj_dependency_real}" "${_ksj_runtime_output_dir_real}/" _ksj_dependency_in_output)
  if(_ksj_dependency_in_output EQUAL 0)
    continue()
  endif()

  set(_ksj_dependency_is_owned FALSE)
  foreach(_ksj_runtime_source_root IN LISTS _ksj_runtime_source_roots)
    string(FIND "${_ksj_dependency_real}" "${_ksj_runtime_source_root}/" _ksj_dependency_in_source_root)
    if(_ksj_dependency_in_source_root EQUAL 0)
      set(_ksj_dependency_is_owned TRUE)
      break()
    endif()
  endforeach()

  if(_ksj_dependency_is_owned)
    list(APPEND _ksj_runtime_dependencies_to_copy "${_ksj_dependency}")
  else()
    list(APPEND _ksj_unowned_runtime_dependencies "${_ksj_dependency}")
  endif()
endforeach()
if(_ksj_unowned_runtime_dependencies)
  list(JOIN _ksj_unowned_runtime_dependencies ", " _ksj_unowned_runtime_dependencies_text)
  message(FATAL_ERROR "Resolved runtime dependencies are outside the approved Conan/Intel roots: "
                      "${_ksj_unowned_runtime_dependencies_text}")
endif()
if(_ksj_runtime_dependencies_to_copy)
  list(REMOVE_DUPLICATES _ksj_runtime_dependencies_to_copy)
endif()
if(_ksj_conflicting_dependencies_to_copy)
  list(APPEND _ksj_runtime_dependencies_to_copy ${_ksj_conflicting_dependencies_to_copy})
  list(REMOVE_DUPLICATES _ksj_runtime_dependencies_to_copy)
endif()

if(DEFINED KSJ_RUNTIME_MODULE_OUTPUT_DIR AND NOT KSJ_RUNTIME_MODULE_OUTPUT_DIR STREQUAL "")
  file(MAKE_DIRECTORY "${KSJ_RUNTIME_MODULE_OUTPUT_DIR}")
  foreach(_ksj_runtime_module IN LISTS _ksj_runtime_modules)
    file(
      COPY "${_ksj_runtime_module}"
      DESTINATION "${KSJ_RUNTIME_MODULE_OUTPUT_DIR}"
      FOLLOW_SYMLINK_CHAIN)
  endforeach()
endif()
foreach(_ksj_dependency IN LISTS _ksj_runtime_dependencies_to_copy)
  # FOLLOW_SYMLINK_CHAIN keeps the ELF SONAME links, for example libicui18n.so.78 -> .78.2.
  file(
    COPY "${_ksj_dependency}"
    DESTINATION "${KSJ_RUNTIME_OUTPUT_DIR}"
    FOLLOW_SYMLINK_CHAIN)
endforeach()

list(LENGTH _ksj_runtime_dependencies_to_copy _ksj_runtime_dependency_count)
list(LENGTH _ksj_runtime_modules _ksj_runtime_module_count)
message(STATUS "Staged ${_ksj_runtime_dependency_count} Linux runtime shared libraries and "
               "${_ksj_runtime_module_count} modules to ${KSJ_RUNTIME_OUTPUT_DIR}")
