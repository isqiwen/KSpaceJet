ksj_include_project_bootstrap()

set(KSJ_CORE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(KSJ_BASE_VERSION "0.0.0-dev")
if(DEFINED PROJECT_VERSION AND NOT "${PROJECT_VERSION}" STREQUAL "")
  set(KSJ_BASE_VERSION "${PROJECT_VERSION}")
endif()

file(MAKE_DIRECTORY "${KSJ_CORE_GENERATED_DIR}/kspacejet/base")
configure_file("${CMAKE_CURRENT_LIST_DIR}/generated_version.hpp.in"
               "${KSJ_CORE_GENERATED_DIR}/kspacejet/base/generated_version.hpp" @ONLY)

add_library(ksj_core SHARED)
add_library(KSpaceJet::core ALIAS ksj_core)
target_compile_features(ksj_core PUBLIC cxx_std_20)
target_link_libraries(ksj_core PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options)
target_include_directories(ksj_core PRIVATE "${KSJ_CORE_GENERATED_DIR}")
set_target_properties(
  ksj_core
  PROPERTIES CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET default
             VISIBILITY_INLINES_HIDDEN OFF
             WINDOWS_EXPORT_ALL_SYMBOLS ON)
ksj_set_library_runtime_path(ksj_core)
ksj_install_target(ksj_core)

function(ksj_add_core_component component_name)
  set(options)
  set(oneValueArgs INCLUDE_DIR)
  set(multiValueArgs INCLUDE_DIRS PRIVATE_INCLUDE_DIRS SOURCES PUBLIC_LINK_LIBS PRIVATE_LINK_LIBS)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(_ksj_core_include_dirs ${ARG_INCLUDE_DIR} ${ARG_INCLUDE_DIRS})
  if(NOT _ksj_core_include_dirs)
    message(FATAL_ERROR "ksj_add_core_component(${component_name}) requires INCLUDE_DIR or INCLUDE_DIRS")
  endif()

  set(_ksj_core_sources)
  foreach(_ksj_core_source IN LISTS ARG_SOURCES)
    if(IS_ABSOLUTE "${_ksj_core_source}")
      list(APPEND _ksj_core_sources "${_ksj_core_source}")
    else()
      list(APPEND _ksj_core_sources "${CMAKE_CURRENT_SOURCE_DIR}/${_ksj_core_source}")
    endif()
  endforeach()

  if(_ksj_core_sources)
    target_sources(ksj_core PRIVATE ${_ksj_core_sources})
  endif()
  target_include_directories(ksj_core PUBLIC ${_ksj_core_include_dirs})
  if(ARG_PRIVATE_INCLUDE_DIRS)
    target_include_directories(ksj_core PRIVATE ${ARG_PRIVATE_INCLUDE_DIRS})
  endif()

  if(ARG_PUBLIC_LINK_LIBS)
    target_link_libraries(ksj_core PUBLIC ${ARG_PUBLIC_LINK_LIBS})
  endif()

  if(ARG_PRIVATE_LINK_LIBS)
    target_link_libraries(ksj_core PRIVATE ${ARG_PRIVATE_LINK_LIBS})
  endif()
endfunction()

# Only build the KSpaceJet-owned core foundation/runtime layer from libs/core.
add_subdirectory(kspacejet-base)
add_subdirectory(kspacejet-config)
add_subdirectory(kspacejet-logging)
add_subdirectory(kspacejet-crash)
add_subdirectory(kspacejet-platform)
add_subdirectory(kspacejet-process-runtime)
add_subdirectory(kspacejet-performance)
add_subdirectory(kspacejet-threading)
add_subdirectory(kspacejet-memory)
add_subdirectory(kspacejet-program)
