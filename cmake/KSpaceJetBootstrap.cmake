include_guard()

include(CMakeParseArguments)
include(GNUInstallDirs)

option(KSJ_ENABLE_CCACHE "Use ccache when it is available." ON)
option(KSJ_WARNINGS_AS_ERRORS "Treat KSpaceJet compiler warnings as errors." OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE
      "Release"
      CACHE STRING "Build type" FORCE)
endif()

if(KSJ_ENABLE_CCACHE)
  find_program(KSJ_CCACHE_EXECUTABLE ccache)
  if(KSJ_CCACHE_EXECUTABLE)
    set(CMAKE_C_COMPILER_LAUNCHER
        "${KSJ_CCACHE_EXECUTABLE}"
        CACHE STRING "" FORCE)
    set(CMAKE_CXX_COMPILER_LAUNCHER
        "${KSJ_CCACHE_EXECUTABLE}"
        CACHE STRING "" FORCE)
  endif()
endif()

add_library(ksj_project_options INTERFACE)
add_library(KSpaceJet::project_options ALIAS ksj_project_options)
set(PROJECT_OPTIONS_TARGET ksj_project_options)

if(KSJ_WARNINGS_AS_ERRORS)
  target_compile_options(ksj_project_options INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/WX>
                                                       $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>)
endif()

function(project_kit_install_targets)
  set(options)
  set(one_value_args RUNTIME_DEPENDENCY_SET RUNTIME_DESTINATION LIBRARY_DESTINATION ARCHIVE_DESTINATION COMPONENT)
  set(multi_value_args TARGETS)
  cmake_parse_arguments(KSJ_INSTALL "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT KSJ_INSTALL_TARGETS)
    message(FATAL_ERROR "project_kit_install_targets() requires TARGETS.")
  endif()

  set(_ksj_install_args TARGETS ${KSJ_INSTALL_TARGETS})
  if(KSJ_INSTALL_RUNTIME_DEPENDENCY_SET)
    list(APPEND _ksj_install_args RUNTIME_DEPENDENCY_SET ${KSJ_INSTALL_RUNTIME_DEPENDENCY_SET})
  endif()

  set(_ksj_runtime_destination "${CMAKE_INSTALL_BINDIR}")
  set(_ksj_library_destination "${CMAKE_INSTALL_LIBDIR}")
  set(_ksj_archive_destination "${CMAKE_INSTALL_LIBDIR}")
  if(KSJ_INSTALL_RUNTIME_DESTINATION)
    set(_ksj_runtime_destination "${KSJ_INSTALL_RUNTIME_DESTINATION}")
  endif()
  if(KSJ_INSTALL_LIBRARY_DESTINATION)
    set(_ksj_library_destination "${KSJ_INSTALL_LIBRARY_DESTINATION}")
  endif()
  if(KSJ_INSTALL_ARCHIVE_DESTINATION)
    set(_ksj_archive_destination "${KSJ_INSTALL_ARCHIVE_DESTINATION}")
  endif()

  list(
    APPEND
    _ksj_install_args
    RUNTIME
    DESTINATION
    "${_ksj_runtime_destination}"
    LIBRARY
    DESTINATION
    "${_ksj_library_destination}"
    ARCHIVE
    DESTINATION
    "${_ksj_archive_destination}")
  if(KSJ_INSTALL_COMPONENT)
    list(APPEND _ksj_install_args COMPONENT ${KSJ_INSTALL_COMPONENT})
  endif()
  install(${_ksj_install_args})
endfunction()

function(project_kit_add_test)
  if(NOT BUILD_TESTING)
    return()
  endif()

  set(options WILL_FAIL)
  set(one_value_args NAME TARGET WORKING_DIRECTORY)
  set(multi_value_args COMMAND ARGS ENVIRONMENT LABELS)
  cmake_parse_arguments(KSJ_TEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT KSJ_TEST_NAME)
    message(FATAL_ERROR "project_kit_add_test() requires NAME.")
  endif()
  if(KSJ_TEST_TARGET)
    if(NOT TARGET ${KSJ_TEST_TARGET})
      message(FATAL_ERROR "project_kit_add_test() target does not exist: ${KSJ_TEST_TARGET}")
    endif()
    add_test(NAME ${KSJ_TEST_NAME} COMMAND $<TARGET_FILE:${KSJ_TEST_TARGET}> ${KSJ_TEST_ARGS})
  elseif(KSJ_TEST_COMMAND)
    add_test(NAME ${KSJ_TEST_NAME} COMMAND ${KSJ_TEST_COMMAND})
  else()
    message(FATAL_ERROR "project_kit_add_test() requires TARGET or COMMAND.")
  endif()

  set(_ksj_test_properties)
  if(KSJ_TEST_WORKING_DIRECTORY)
    list(APPEND _ksj_test_properties WORKING_DIRECTORY "${KSJ_TEST_WORKING_DIRECTORY}")
  endif()
  if(KSJ_TEST_ENVIRONMENT)
    list(APPEND _ksj_test_properties ENVIRONMENT "${KSJ_TEST_ENVIRONMENT}")
  endif()
  if(KSJ_TEST_WILL_FAIL)
    list(APPEND _ksj_test_properties WILL_FAIL TRUE)
  endif()
  if(_ksj_test_properties)
    set_tests_properties(${KSJ_TEST_NAME} PROPERTIES ${_ksj_test_properties})
  endif()
  if(KSJ_TEST_LABELS)
    set_property(
      TEST ${KSJ_TEST_NAME}
      APPEND
      PROPERTY LABELS ${KSJ_TEST_LABELS})
  endif()
endfunction()
