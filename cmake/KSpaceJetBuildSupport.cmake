include_guard()

include(CMakeParseArguments)
include(GNUInstallDirs)

function(ksj_use_origin_install_rpath target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "ksj_use_origin_install_rpath() requires an existing target: ${target_name}")
  endif()

  if(WIN32)
    return()
  endif()

  get_target_property(_ksj_target_imported ${target_name} IMPORTED)
  if(_ksj_target_imported)
    return()
  endif()

  get_target_property(_ksj_target_type ${target_name} TYPE)
  if(_ksj_target_type STREQUAL "EXECUTABLE")
    set(_ksj_install_rpath "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  elseif(_ksj_target_type STREQUAL "SHARED_LIBRARY" OR _ksj_target_type STREQUAL "MODULE_LIBRARY")
    set(_ksj_install_rpath "\$ORIGIN")
  else()
    return()
  endif()

  # DT_RPATH lets copied third-party libraries resolve transitive dependencies from the install tree before falling back
  # to system paths.
  target_link_options(${target_name} PRIVATE -Wl,--disable-new-dtags)
  set_target_properties(
    ${target_name}
    PROPERTIES BUILD_RPATH_USE_ORIGIN ON
               INSTALL_RPATH "${_ksj_install_rpath}"
               INSTALL_RPATH_USE_LINK_PATH FALSE)
endfunction()

# Conan packages expose imported targets instead of a repository-wide prefix directory. Derive runtime search paths from
# those targets so neither the build nor install tree relies on a locally installed oneAPI SDK.
function(ksj_collect_imported_target_runtime_dirs output_variable)
  set(_ksj_runtime_dirs)
  foreach(_ksj_target IN LISTS ARGN)
    if(NOT TARGET "${_ksj_target}")
      continue()
    endif()

    get_target_property(_ksj_imported "${_ksj_target}" IMPORTED)
    if(NOT _ksj_imported)
      continue()
    endif()

    set(_ksj_location_properties IMPORTED_LOCATION IMPORTED_IMPLIB)
    get_target_property(_ksj_imported_configurations "${_ksj_target}" IMPORTED_CONFIGURATIONS)
    if(_ksj_imported_configurations MATCHES "-NOTFOUND$")
      unset(_ksj_imported_configurations)
    endif()
    foreach(_ksj_configuration IN LISTS _ksj_imported_configurations)
      string(TOUPPER "${_ksj_configuration}" _ksj_configuration_upper)
      list(APPEND _ksj_location_properties "IMPORTED_LOCATION_${_ksj_configuration_upper}"
           "IMPORTED_IMPLIB_${_ksj_configuration_upper}")
    endforeach()

    foreach(_ksj_location_property IN LISTS _ksj_location_properties)
      get_target_property(_ksj_location "${_ksj_target}" "${_ksj_location_property}")
      if(_ksj_location AND EXISTS "${_ksj_location}")
        get_filename_component(_ksj_runtime_dir "${_ksj_location}" DIRECTORY)
        list(APPEND _ksj_runtime_dirs "${_ksj_runtime_dir}")
      endif()
    endforeach()
  endforeach()

  if(_ksj_runtime_dirs)
    list(REMOVE_DUPLICATES _ksj_runtime_dirs)
  endif()
  set(${output_variable}
      "${_ksj_runtime_dirs}"
      PARENT_SCOPE)
endfunction()

# CMakeDeps exposes package binary directories through variables such as "foo_BIN_DIRS_RELEASE".  On Windows an imported
# target can point at an import library instead of its DLL, so the package bindir is the reliable search root for
# file(GET_RUNTIME_DEPENDENCIES).  Walk the known directory tree because packages may be found from an application
# subdirectory.
function(_ksj_collect_directory_conan_runtime_dirs output_variable directory_path)
  set(_ksj_runtime_dirs)

  get_directory_property(_ksj_directory_variables DIRECTORY "${directory_path}" VARIABLES)
  foreach(_ksj_variable IN LISTS _ksj_directory_variables)
    if(NOT _ksj_variable MATCHES "_BIN_DIRS(_[A-Z0-9_]+)?$")
      continue()
    endif()

    get_directory_property(_ksj_variable_value DIRECTORY "${directory_path}" DEFINITION "${_ksj_variable}")
    if(_ksj_variable_value)
      list(APPEND _ksj_runtime_dirs ${_ksj_variable_value})
    endif()
  endforeach()

  get_property(
    _ksj_subdirectories
    DIRECTORY "${directory_path}"
    PROPERTY SUBDIRECTORIES)
  foreach(_ksj_subdirectory IN LISTS _ksj_subdirectories)
    _ksj_collect_directory_conan_runtime_dirs(_ksj_subdirectory_runtime_dirs "${_ksj_subdirectory}")
    list(APPEND _ksj_runtime_dirs ${_ksj_subdirectory_runtime_dirs})
  endforeach()

  if(_ksj_runtime_dirs)
    list(REMOVE_DUPLICATES _ksj_runtime_dirs)
  endif()
  set(${output_variable}
      "${_ksj_runtime_dirs}"
      PARENT_SCOPE)
endfunction()

function(ksj_collect_conan_runtime_dirs output_variable)
  _ksj_collect_directory_conan_runtime_dirs(_ksj_runtime_dirs "${CMAKE_SOURCE_DIR}")
  set(${output_variable}
      "${_ksj_runtime_dirs}"
      PARENT_SCOPE)
endfunction()

function(ksj_get_intel_runtime_dirs output_variable)
  ksj_collect_imported_target_runtime_dirs(
    _ksj_intel_runtime_dirs
    Intel::ippcore
    Intel::ippi
    Intel::ipps
    Intel::ippcv
    Intel::ippdc
    Intel::ippvm
    Intel::ippcore_tl_omp
    Intel::mkl_rt
    Intel::iomp5)
  if(DEFINED KSJ_INTEL_RUNTIME_DIRS)
    list(APPEND _ksj_intel_runtime_dirs ${KSJ_INTEL_RUNTIME_DIRS})
  endif()
  if(_ksj_intel_runtime_dirs)
    list(REMOVE_DUPLICATES _ksj_intel_runtime_dirs)
  endif()
  set(${output_variable}
      "${_ksj_intel_runtime_dirs}"
      PARENT_SCOPE)
endfunction()

function(ksj_install_thirdparty_runtime_dependencies dependency_set_name)
  if(NOT KSJ_ENABLE_INSTALL_RULES)
    return()
  endif()

  ksj_get_intel_runtime_dirs(_ksj_intel_runtime_dirs)
  set(_ksj_runtime_dirs ${_ksj_intel_runtime_dirs})
  if(WIN32)
    # CMake's install(RUNTIME_DEPENDENCY_SET) scans the installed KSpaceJet binaries, but it needs Conan package bin
    # directories to resolve their transitive DLLs.  Keep the Intel-specific paths above for its dispatch DLLs and add
    # every CMakeDeps package bindir as a general mechanism.
    ksj_collect_conan_runtime_dirs(_ksj_conan_runtime_dirs)
    list(APPEND _ksj_runtime_dirs ${_ksj_conan_runtime_dirs})
  endif()
  foreach(_ksj_runtime_dir IN LISTS _ksj_runtime_dirs)
    if(IS_DIRECTORY "${_ksj_runtime_dir}")
      list(APPEND _ksj_existing_runtime_dirs "${_ksj_runtime_dir}")
    endif()
  endforeach()
  # TARGET_RUNTIME_DLLS in the generated staging script remains a direct-DLL fallback for imported targets that do not
  # publish a CMakeDeps bindir.
  if(_ksj_existing_runtime_dirs)
    list(REMOVE_DUPLICATES _ksj_existing_runtime_dirs)
  endif()

  set(_ksj_install_dependency_args RUNTIME_DEPENDENCY_SET ${dependency_set_name})
  if(WIN32)
    list(
      APPEND
      _ksj_install_dependency_args
      PRE_EXCLUDE_REGEXES
      "api-ms-.*"
      "ext-ms-.*"
      POST_EXCLUDE_REGEXES
      ".*[Ww]indows[\\\\/].*"
      ".*[Ss]ystem32[\\\\/].*")
  else()
    list(
      APPEND
      _ksj_install_dependency_args
      PRE_EXCLUDE_REGEXES
      "linux-vdso\\.so.*"
      POST_EXCLUDE_REGEXES
      "^/lib/.*"
      "^/lib64/.*"
      "^/usr/lib/.*"
      "^/usr/lib64/.*")
  endif()
  if(_ksj_existing_runtime_dirs)
    list(APPEND _ksj_install_dependency_args DIRECTORIES ${_ksj_existing_runtime_dirs})
  endif()
  list(
    APPEND
    _ksj_install_dependency_args
    LIBRARY
    DESTINATION
    "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME
    DESTINATION
    "${CMAKE_INSTALL_BINDIR}")

  install(${_ksj_install_dependency_args})
endfunction()

function(ksj_install_intel_runtime_libraries)
  if(NOT KSJ_ENABLE_INSTALL_RULES)
    return()
  endif()

  ksj_get_intel_runtime_dirs(_ksj_intel_runtime_dirs)

  set(_ksj_intel_runtime_libraries)
  foreach(_ksj_intel_runtime_dir IN LISTS _ksj_intel_runtime_dirs)
    if(IS_DIRECTORY "${_ksj_intel_runtime_dir}")
      if(WIN32)
        file(
          GLOB_RECURSE _ksj_intel_dir_libraries
          LIST_DIRECTORIES false
          CONFIGURE_DEPENDS "${_ksj_intel_runtime_dir}/*.dll")
      else()
        file(
          GLOB_RECURSE _ksj_intel_dir_libraries
          LIST_DIRECTORIES false
          CONFIGURE_DEPENDS "${_ksj_intel_runtime_dir}/*.so" "${_ksj_intel_runtime_dir}/*.so.*")
      endif()
      list(APPEND _ksj_intel_runtime_libraries ${_ksj_intel_dir_libraries})
    endif()
  endforeach()
  if(_ksj_intel_runtime_libraries)
    list(REMOVE_DUPLICATES _ksj_intel_runtime_libraries)
    if(WIN32)
      install(FILES ${_ksj_intel_runtime_libraries} DESTINATION "${CMAKE_INSTALL_BINDIR}")
    else()
      install(FILES ${_ksj_intel_runtime_libraries} DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()
  endif()
endfunction()

function(KSJ_INCLUDE_PROJECT_BOOTSTRAP)
  # Compatibility wrapper for subdirectories imported from the standalone KSpaceJet CMake layout. The repository root
  # already includes KSpaceJetBootstrap.cmake.
endfunction()

function(KSJ_TARGET_ENABLE_OPENMP_COMPILE_ONLY target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "KSJ_TARGET_ENABLE_OPENMP_COMPILE_ONLY() requires an existing target: ${target_name}")
  endif()
  find_package(OpenMP REQUIRED COMPONENTS CXX)

  # Some legacy algorithm modules use the GNU/LLVM OpenMP language extensions while deliberately linking the Intel
  # OpenMP runtime.  Linking OpenMP::OpenMP_CXX here also propagates its runtime (libgomp on GCC), which loads two
  # OpenMP runtimes into one reconstruction process.  Preserve only the target's compile requirements; callers choose
  # the runtime explicitly through their normal link dependencies.
  get_target_property(_ksj_openmp_compile_options OpenMP::OpenMP_CXX INTERFACE_COMPILE_OPTIONS)
  if(_ksj_openmp_compile_options)
    target_compile_options(${target_name} PRIVATE ${_ksj_openmp_compile_options})
  endif()

  get_target_property(_ksj_openmp_include_dirs OpenMP::OpenMP_CXX INTERFACE_INCLUDE_DIRECTORIES)
  if(_ksj_openmp_include_dirs)
    target_include_directories(${target_name} SYSTEM PRIVATE ${_ksj_openmp_include_dirs})
  endif()
endfunction()

function(KSJ_SET_LIBRARY_RUNTIME_PATH target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "KSJ_SET_LIBRARY_RUNTIME_PATH() requires an existing target: ${target_name}")
  endif()
  if(WIN32)
    return()
  endif()
  set_property(
    TARGET ${target_name}
    APPEND
    PROPERTY BUILD_RPATH "$ORIGIN")
  ksj_use_origin_install_rpath(${target_name})
endfunction()

function(KSJ_SET_EXECUTABLE_RUNTIME_PATH target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "KSJ_SET_EXECUTABLE_RUNTIME_PATH() requires an existing target: ${target_name}")
  endif()
  if(WIN32)
    return()
  endif()
  set(_ksj_build_rpath "$ORIGIN/../lib")
  ksj_get_intel_runtime_dirs(_ksj_intel_runtime_dirs)
  list(APPEND _ksj_build_rpath ${_ksj_intel_runtime_dirs})
  list(REMOVE_DUPLICATES _ksj_build_rpath)
  set_property(
    TARGET ${target_name}
    APPEND
    PROPERTY BUILD_RPATH ${_ksj_build_rpath})
  ksj_use_origin_install_rpath(${target_name})
endfunction()

function(KSJ_INSTALL_TARGET target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "KSJ_INSTALL_TARGET() requires an existing target: ${target_name}")
  endif()
  if(NOT KSJ_ENABLE_INSTALL_RULES)
    return()
  endif()

  if(DEFINED KSJ_RUNTIME_DEPENDENCY_SET AND NOT "${KSJ_RUNTIME_DEPENDENCY_SET}" STREQUAL "")
    project_kit_install_targets(TARGETS ${target_name} RUNTIME_DEPENDENCY_SET ${KSJ_RUNTIME_DEPENDENCY_SET})
  else()
    project_kit_install_targets(TARGETS ${target_name})
  endif()
endfunction()

function(ksj_use_flat_output_directories)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
      "${CMAKE_BINARY_DIR}/bin"
      PARENT_SCOPE)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
      "${CMAKE_BINARY_DIR}/lib"
      PARENT_SCOPE)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
      "${CMAKE_BINARY_DIR}/lib"
      PARENT_SCOPE)
  set(EXECUTABLE_DIR
      "${CMAKE_BINARY_DIR}/bin"
      PARENT_SCOPE)

  foreach(_ksj_config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${_ksj_config}" _ksj_config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_ksj_config_upper}
        "${CMAKE_BINARY_DIR}/bin"
        PARENT_SCOPE)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_ksj_config_upper}
        "${CMAKE_BINARY_DIR}/lib"
        PARENT_SCOPE)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_ksj_config_upper}
        "${CMAKE_BINARY_DIR}/lib"
        PARENT_SCOPE)
  endforeach()
endfunction()

function(ksj_stage_thirdparty_runtime_dlls target_name)
  if(NOT WIN32)
    return()
  endif()
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "ksj_stage_thirdparty_runtime_dlls() requires an existing target: ${target_name}")
  endif()

  ksj_get_intel_runtime_dirs(_ksj_intel_runtime_dirs)
  set(_ksj_runtime_dirs ${_ksj_intel_runtime_dirs})
  ksj_collect_conan_runtime_dirs(_ksj_conan_runtime_dirs)
  list(APPEND _ksj_runtime_dirs ${_ksj_conan_runtime_dirs})

  set(_ksj_opencv_library_dirs ${OpenCV_LIBRARY_DIRS})
  if(DEFINED OpenCV_LIB_PATH AND NOT "${OpenCV_LIB_PATH}" STREQUAL "")
    list(APPEND _ksj_opencv_library_dirs "${OpenCV_LIB_PATH}")
  endif()

  foreach(_ksj_opencv_library_dir IN LISTS _ksj_opencv_library_dirs)
    get_filename_component(_ksj_opencv_runtime_parent "${_ksj_opencv_library_dir}" DIRECTORY)
    if(IS_DIRECTORY "${_ksj_opencv_runtime_parent}/bin")
      list(APPEND _ksj_runtime_dirs "${_ksj_opencv_runtime_parent}/bin")
    endif()
  endforeach()
  if(DEFINED OpenCV_DIR AND NOT "${OpenCV_DIR}" STREQUAL "")
    if(IS_DIRECTORY "${OpenCV_DIR}/../bin")
      get_filename_component(_ksj_opencv_runtime_dir "${OpenCV_DIR}/../bin" ABSOLUTE)
      list(APPEND _ksj_runtime_dirs "${_ksj_opencv_runtime_dir}")
    endif()
    file(
      GLOB _ksj_opencv_runtime_dirs
      LIST_DIRECTORIES true
      "${OpenCV_DIR}/x64/vc*/bin" "${OpenCV_DIR}/x86/vc*/bin")
    list(APPEND _ksj_runtime_dirs ${_ksj_opencv_runtime_dirs})
  endif()

  set(_ksj_existing_runtime_dirs)
  foreach(_ksj_runtime_dir IN LISTS _ksj_runtime_dirs)
    if(IS_DIRECTORY "${_ksj_runtime_dir}")
      list(APPEND _ksj_existing_runtime_dirs "${_ksj_runtime_dir}")
    endif()
  endforeach()
  if(_ksj_existing_runtime_dirs)
    list(REMOVE_DUPLICATES _ksj_existing_runtime_dirs)
  endif()

  set(_ksj_stage_dependencies ${target_name})

  set(_ksj_runtime_executable_entries "  \"$<TARGET_FILE:${target_name}>\"\n")
  set(_ksj_runtime_library_entries "")
  foreach(_ksj_runtime_target IN LISTS _ksj_runtime_library_targets)
    string(APPEND _ksj_runtime_library_entries "  \"$<TARGET_FILE:${_ksj_runtime_target}>\"\n")
  endforeach()

  set(_ksj_stage_script
      "${CMAKE_BINARY_DIR}/CMakeFiles/ksj_stage_${target_name}_thirdparty_runtime_dlls-$<CONFIG>.cmake")
  set(_ksj_stage_script_content "set(_ksj_runtime_dirs\n")
  foreach(_ksj_runtime_dir IN LISTS _ksj_existing_runtime_dirs)
    string(REPLACE "\\" "\\\\" _ksj_runtime_dir_escaped "${_ksj_runtime_dir}")
    string(REPLACE "\"" "\\\"" _ksj_runtime_dir_escaped "${_ksj_runtime_dir_escaped}")
    string(APPEND _ksj_stage_script_content "  \"${_ksj_runtime_dir_escaped}\"\n")
  endforeach()
  string(
    APPEND
    _ksj_stage_script_content
    ")\n"
    "set(_ksj_runtime_executables\n"
    "${_ksj_runtime_executable_entries}"
    ")\n"
    "set(_ksj_runtime_libraries\n"
    "${_ksj_runtime_library_entries}"
    ")\n"
    "set(_ksj_target_runtime_dlls\n"
    "  \"$<TARGET_RUNTIME_DLLS:${target_name}>\"\n"
    ")\n"
    "if(NOT DEFINED KSJ_RUNTIME_OUTPUT_DIR OR KSJ_RUNTIME_OUTPUT_DIR STREQUAL \"\")\n"
    "  message(FATAL_ERROR \"KSJ_RUNTIME_OUTPUT_DIR is required.\")\n"
    "endif()\n"
    "file(MAKE_DIRECTORY \"\${KSJ_RUNTIME_OUTPUT_DIR}\")\n"
    "file(REAL_PATH \"\${KSJ_RUNTIME_OUTPUT_DIR}\" _ksj_runtime_output_dir_real)\n"
    "string(TOLOWER \"\${_ksj_runtime_output_dir_real}\" _ksj_runtime_output_dir_real_lower)\n"
    "set(_ksj_target_runtime_dlls_to_copy)\n"
    "foreach(_ksj_target_runtime_dll IN LISTS _ksj_target_runtime_dlls)\n"
    "  if(EXISTS \"\${_ksj_target_runtime_dll}\")\n"
    "    get_filename_component(_ksj_target_runtime_dll_dir \"\${_ksj_target_runtime_dll}\" DIRECTORY)\n"
    "    file(REAL_PATH \"\${_ksj_target_runtime_dll_dir}\" _ksj_target_runtime_dll_dir_real)\n"
    "    string(TOLOWER \"\${_ksj_target_runtime_dll_dir_real}\" _ksj_target_runtime_dll_dir_real_lower)\n"
    "    if(NOT _ksj_target_runtime_dll_dir_real_lower STREQUAL _ksj_runtime_output_dir_real_lower)\n"
    "      list(APPEND _ksj_runtime_dirs \"\${_ksj_target_runtime_dll_dir}\")\n"
    "    endif()\n"
    "    get_filename_component(_ksj_target_runtime_dll_name \"\${_ksj_target_runtime_dll}\" NAME)\n"
    "    set(_ksj_target_runtime_dll_destination \"\${KSJ_RUNTIME_OUTPUT_DIR}/\${_ksj_target_runtime_dll_name}\")\n"
    "    set(_ksj_target_runtime_dll_should_copy TRUE)\n"
    "    if(EXISTS \"\${_ksj_target_runtime_dll_destination}\")\n"
    "      file(REAL_PATH \"\${_ksj_target_runtime_dll}\" _ksj_target_runtime_dll_real)\n"
    "      file(REAL_PATH \"\${_ksj_target_runtime_dll_destination}\" _ksj_target_runtime_dll_destination_real)\n"
    "      string(TOLOWER \"\${_ksj_target_runtime_dll_real}\" _ksj_target_runtime_dll_real_lower)\n"
    "      string(TOLOWER \"\${_ksj_target_runtime_dll_destination_real}\" _ksj_target_runtime_dll_destination_real_lower)\n"
    "      if(_ksj_target_runtime_dll_real_lower STREQUAL _ksj_target_runtime_dll_destination_real_lower)\n"
    "        set(_ksj_target_runtime_dll_should_copy FALSE)\n"
    "      endif()\n"
    "    endif()\n"
    "    if(_ksj_target_runtime_dll_should_copy)\n"
    "      list(APPEND _ksj_target_runtime_dlls_to_copy \"\${_ksj_target_runtime_dll}\")\n"
    "    endif()\n"
    "  endif()\n"
    "endforeach()\n"
    "if(_ksj_runtime_dirs)\n"
    "  list(REMOVE_DUPLICATES _ksj_runtime_dirs)\n"
    "endif()\n"
    "function(_ksj_append_existing_files out_var)\n"
    "  set(_ksj_existing_files)\n"
    "  foreach(_ksj_file IN LISTS ARGN)\n"
    "    if(EXISTS \"\${_ksj_file}\")\n"
    "      list(APPEND _ksj_existing_files \"\${_ksj_file}\")\n"
    "    endif()\n"
    "  endforeach()\n"
    "  set(\${out_var} \"\${_ksj_existing_files}\" PARENT_SCOPE)\n"
    "endfunction()\n"
    "_ksj_append_existing_files(_ksj_existing_runtime_executables \${_ksj_runtime_executables})\n"
    "_ksj_append_existing_files(_ksj_existing_runtime_libraries \${_ksj_runtime_libraries})\n"
    "if(NOT _ksj_existing_runtime_executables AND NOT _ksj_existing_runtime_libraries)\n"
    "  message(FATAL_ERROR \"No runtime files exist for dependency scanning.\")\n"
    "endif()\n"
    "set(_ksj_get_runtime_dependency_args)\n"
    "if(_ksj_existing_runtime_executables)\n"
    "  list(APPEND _ksj_get_runtime_dependency_args EXECUTABLES \${_ksj_existing_runtime_executables})\n"
    "endif()\n"
    "if(_ksj_existing_runtime_libraries)\n"
    "  list(APPEND _ksj_get_runtime_dependency_args LIBRARIES \${_ksj_existing_runtime_libraries})\n"
    "endif()\n"
    "if(_ksj_runtime_dirs)\n"
    "  list(APPEND _ksj_get_runtime_dependency_args DIRECTORIES \${_ksj_runtime_dirs})\n"
    "endif()\n"
    "file(GET_RUNTIME_DEPENDENCIES\n"
    "  RESOLVED_DEPENDENCIES_VAR _ksj_resolved_dependencies\n"
    "  UNRESOLVED_DEPENDENCIES_VAR _ksj_unresolved_dependencies\n"
    "  CONFLICTING_DEPENDENCIES_PREFIX _ksj_conflicting_dependencies\n"
    "  \${_ksj_get_runtime_dependency_args}\n"
    "  PRE_EXCLUDE_REGEXES \"api-ms-.*\" \"ext-ms-.*\"\n"
    "  POST_EXCLUDE_REGEXES \".*[Ww]indows[/\\\\][Ss]ystem32[/\\\\].*\" \".*[Ww]indows[/\\\\][Ss]ysWOW64[/\\\\].*\"\n"
    ")\n"
    "set(_ksj_runtime_dir_reals)\n"
    "foreach(_ksj_runtime_dir IN LISTS _ksj_runtime_dirs)\n"
    "  if(IS_DIRECTORY \"\${_ksj_runtime_dir}\")\n"
    "    file(REAL_PATH \"\${_ksj_runtime_dir}\" _ksj_runtime_dir_real)\n"
    "    string(TOLOWER \"\${_ksj_runtime_dir_real}\" _ksj_runtime_dir_real_lower)\n"
    "    list(APPEND _ksj_runtime_dir_reals \"\${_ksj_runtime_dir_real_lower}\")\n"
    "  endif()\n"
    "endforeach()\n"
    "set(_ksj_runtime_dependencies_to_copy)\n"
    "foreach(_ksj_dependency IN LISTS _ksj_resolved_dependencies)\n"
    "  if(EXISTS \"\${_ksj_dependency}\")\n"
    "    file(REAL_PATH \"\${_ksj_dependency}\" _ksj_dependency_real)\n"
    "    string(TOLOWER \"\${_ksj_dependency_real}\" _ksj_dependency_real_lower)\n"
    "    foreach(_ksj_runtime_dir_real IN LISTS _ksj_runtime_dir_reals)\n"
    "      string(FIND \"\${_ksj_dependency_real_lower}\" \"\${_ksj_runtime_dir_real}/\" _ksj_dependency_dir_pos)\n"
    "      if(_ksj_dependency_dir_pos EQUAL 0)\n"
    "        list(APPEND _ksj_runtime_dependencies_to_copy \"\${_ksj_dependency}\")\n"
    "        break()\n"
    "      endif()\n"
    "    endforeach()\n"
    "  endif()\n"
    "endforeach()\n"
    "foreach(_ksj_conflicting_filename IN LISTS _ksj_conflicting_dependencies_FILENAMES)\n"
    "  set(_ksj_conflicting_paths \"\${_ksj_conflicting_dependencies_\${_ksj_conflicting_filename}}\")\n"
    "  foreach(_ksj_conflicting_path IN LISTS _ksj_conflicting_paths)\n"
    "    if(EXISTS \"\${_ksj_conflicting_path}\")\n"
    "      file(REAL_PATH \"\${_ksj_conflicting_path}\" _ksj_conflicting_path_real)\n"
    "      string(TOLOWER \"\${_ksj_conflicting_path_real}\" _ksj_conflicting_path_real_lower)\n"
    "      foreach(_ksj_runtime_dir_real IN LISTS _ksj_runtime_dir_reals)\n"
    "        string(FIND \"\${_ksj_conflicting_path_real_lower}\" \"\${_ksj_runtime_dir_real}/\" _ksj_conflicting_dir_pos)\n"
    "        if(_ksj_conflicting_dir_pos EQUAL 0)\n"
    "          list(APPEND _ksj_runtime_dependencies_to_copy \"\${_ksj_conflicting_path}\")\n"
    "          break()\n"
    "        endif()\n"
    "      endforeach()\n"
    "    endif()\n"
    "  endforeach()\n"
    "endforeach()\n"
    "list(REMOVE_DUPLICATES _ksj_runtime_dependencies_to_copy)\n"
    "if(_ksj_target_runtime_dlls_to_copy)\n"
    "  list(APPEND _ksj_runtime_dependencies_to_copy \${_ksj_target_runtime_dlls_to_copy})\n"
    "  list(REMOVE_DUPLICATES _ksj_runtime_dependencies_to_copy)\n"
    "endif()\n"
    "set(_ksj_intel_dispatch_dlls)\n"
    "foreach(_ksj_runtime_dir IN LISTS _ksj_runtime_dirs)\n"
    "  if(IS_DIRECTORY \"\${_ksj_runtime_dir}\")\n"
    "    file(GLOB _ksj_runtime_dir_intel_dispatch_dlls LIST_DIRECTORIES false\n"
    "      \"\${_ksj_runtime_dir}/ipp*.dll\"\n"
    "      \"\${_ksj_runtime_dir}/mkl*.dll\"\n"
    "      \"\${_ksj_runtime_dir}/libiomp5md.dll\"\n"
    "      \"\${_ksj_runtime_dir}/libiompstubs5md.dll\")\n"
    "    list(APPEND _ksj_intel_dispatch_dlls \${_ksj_runtime_dir_intel_dispatch_dlls})\n"
    "  endif()\n"
    "endforeach()\n"
    "if(_ksj_intel_dispatch_dlls)\n"
    "  list(APPEND _ksj_runtime_dependencies_to_copy \${_ksj_intel_dispatch_dlls})\n"
    "  list(REMOVE_DUPLICATES _ksj_runtime_dependencies_to_copy)\n"
    "endif()\n"
    "foreach(_ksj_runtime_dll IN LISTS _ksj_runtime_dependencies_to_copy)\n"
    "  execute_process(\n"
    "    COMMAND \"${CMAKE_COMMAND}\" -E copy_if_different \"\${_ksj_runtime_dll}\" \"\${KSJ_RUNTIME_OUTPUT_DIR}\"\n"
    "    RESULT_VARIABLE _ksj_copy_result)\n"
    "  if(NOT \${_ksj_copy_result} EQUAL 0)\n"
    "    message(FATAL_ERROR \"Failed to copy \${_ksj_runtime_dll} to \${KSJ_RUNTIME_OUTPUT_DIR}\")\n"
    "  endif()\n"
    "endforeach()\n"
    "set(_ksj_report_unresolved_dependencies)\n"
    "foreach(_ksj_unresolved_dependency IN LISTS _ksj_unresolved_dependencies)\n"
    "  get_filename_component(_ksj_unresolved_name \"\${_ksj_unresolved_dependency}\" NAME)\n"
    "  string(TOLOWER \"\${_ksj_unresolved_name}\" _ksj_unresolved_name_lower)\n"
    "  if(NOT _ksj_unresolved_name_lower MATCHES \"^(api-ms-|ext-ms-|kernel32\\\\.dll|user32\\\\.dll|gdi32\\\\.dll|advapi32\\\\.dll|shell32\\\\.dll|ole32\\\\.dll|oleaut32\\\\.dll|ws2_32\\\\.dll|psapi\\\\.dll|version\\\\.dll|dbghelp\\\\.dll|msvcp[0-9]+d?\\\\.dll|vcruntime[0-9]+d?\\\\.dll|ucrtbase(d)?\\\\.dll|vcomp[0-9]+d?\\\\.dll|concrt[0-9]+d?\\\\.dll)$\")\n"
    "    list(APPEND _ksj_report_unresolved_dependencies \"\${_ksj_unresolved_dependency}\")\n"
    "  endif()\n"
    "endforeach()\n"
    "if(_ksj_report_unresolved_dependencies)\n"
    "  list(JOIN _ksj_report_unresolved_dependencies \", \" _ksj_unresolved_dependencies_text)\n"
    "  message(WARNING \"Unresolved runtime dependencies: \${_ksj_unresolved_dependencies_text}\")\n"
    "endif()\n"
    "list(LENGTH _ksj_runtime_dependencies_to_copy _ksj_runtime_dependency_count)\n"
    "message(STATUS \"Staged \${_ksj_runtime_dependency_count} runtime DLLs to \${KSJ_RUNTIME_OUTPUT_DIR}\")\n")
  file(
    GENERATE
    OUTPUT "${_ksj_stage_script}"
    CONTENT "${_ksj_stage_script_content}")

  set(_ksj_runtime_destinations "$<TARGET_FILE_DIR:${target_name}>")
  foreach(_ksj_extra_destination IN LISTS ARGN)
    if(NOT "${_ksj_extra_destination}" STREQUAL "")
      list(APPEND _ksj_runtime_destinations "${_ksj_extra_destination}")
    endif()
  endforeach()

  set(_ksj_stage_commands "")
  foreach(_ksj_runtime_destination IN LISTS _ksj_runtime_destinations)
    list(
      APPEND
      _ksj_stage_commands
      COMMAND
      ${CMAKE_COMMAND}
      "-DKSJ_RUNTIME_OUTPUT_DIR=${_ksj_runtime_destination}"
      -P
      "${_ksj_stage_script}")
  endforeach()

  add_custom_target(
    ${target_name}_stage_thirdparty_runtime_dlls ALL
    ${_ksj_stage_commands}
    DEPENDS ${_ksj_stage_dependencies}
    VERBATIM)
endfunction()

function(ksj_target_enable_defaults target_name)
  set(options OPENMP)
  set(oneValueArgs CXX_STANDARD LINK_SCOPE)
  cmake_parse_arguments(KSpaceJet "${options}" "${oneValueArgs}" "" ${ARGN})

  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "ksj_target_enable_defaults() requires an existing target: ${target_name}")
  endif()

  if(NOT KSJ_CXX_STANDARD)
    set(KSJ_CXX_STANDARD 20)
  endif()
  if(NOT KSJ_LINK_SCOPE)
    set(KSJ_LINK_SCOPE PRIVATE)
  endif()

  set_target_properties(
    ${target_name}
    PROPERTIES CXX_STANDARD ${KSJ_CXX_STANDARD}
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO)

  if(TARGET KSpaceJet::compile_options)
    target_link_libraries(${target_name} ${KSJ_LINK_SCOPE} KSpaceJet::compile_options)
  elseif(DEFINED PROJECT_OPTIONS_TARGET AND TARGET ${PROJECT_OPTIONS_TARGET})
    target_link_libraries(${target_name} ${KSJ_LINK_SCOPE} ${PROJECT_OPTIONS_TARGET})
  endif()

  if(KSJ_OPENMP)
    find_package(OpenMP REQUIRED COMPONENTS CXX)
    target_link_libraries(${target_name} ${KSJ_LINK_SCOPE} OpenMP::OpenMP_CXX)
  endif()
endfunction()

function(ksj_refresh_link link_path target_path)
  if(NOT EXISTS "${target_path}" AND NOT IS_SYMLINK "${target_path}")
    return()
  endif()

  if(WIN32)
    file(TO_NATIVE_PATH "${link_path}" _ksj_link_path_native)
    file(TO_NATIVE_PATH "${target_path}" _ksj_target_path_native)

    if(EXISTS "${link_path}" OR IS_SYMLINK "${link_path}")
      execute_process(
        COMMAND cmd /c rmdir "${_ksj_link_path_native}"
        RESULT_VARIABLE _ksj_rmdir_result
        OUTPUT_VARIABLE _ksj_rmdir_output
        ERROR_VARIABLE _ksj_rmdir_error)
      if(NOT _ksj_rmdir_result EQUAL 0)
        message(
          FATAL_ERROR
            "Failed to remove existing Windows directory link ${link_path}. Output: ${_ksj_rmdir_output} ${_ksj_rmdir_error}"
        )
      endif()
    endif()

    execute_process(
      COMMAND cmd /c mklink /J "${_ksj_link_path_native}" "${_ksj_target_path_native}"
      RESULT_VARIABLE _ksj_mklink_result
      OUTPUT_VARIABLE _ksj_mklink_output
      ERROR_VARIABLE _ksj_mklink_error)
    if(NOT _ksj_mklink_result EQUAL 0)
      message(
        FATAL_ERROR
          "Failed to create Windows directory junction ${link_path} -> ${target_path}. Output: ${_ksj_mklink_output} ${_ksj_mklink_error}"
      )
    endif()
    return()
  endif()

  if(EXISTS "${link_path}" OR IS_SYMLINK "${link_path}")
    file(REMOVE_RECURSE "${link_path}")
  endif()

  file(CREATE_LINK "${target_path}" "${link_path}" SYMBOLIC COPY_ON_ERROR)
endfunction()
