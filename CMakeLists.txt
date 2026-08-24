cmake_minimum_required(VERSION 3.25)

project(
  KSpaceJet
  VERSION 7.1.0
  LANGUAGES C CXX)

if(WIN32)
  enable_language(RC)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>" "$<$<COMPILE_LANGUAGE:CXX>:/bigobj>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

option(KSJ_WINDOWS_USE_DYNAMIC_MSVC_RUNTIME "Build Windows KSpaceJet targets with the dynamic MSVC runtime (/MD)." ON)
if(WIN32)
  if(NOT MSVC)
    message(FATAL_ERROR "KSpaceJet supports Windows through the MSVC toolchain. Use the windows-vs2022 CMake presets.")
  endif()

  if(KSJ_WINDOWS_USE_DYNAMIC_MSVC_RUNTIME)
    set(_ksj_msvc_runtime "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  else()
    set(_ksj_msvc_runtime "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  set(CMAKE_MSVC_RUNTIME_LIBRARY
      "${_ksj_msvc_runtime}"
      CACHE STRING "MSVC runtime library selected by KSJ_WINDOWS_USE_DYNAMIC_MSVC_RUNTIME" FORCE)
endif()

option(KSJ_ENABLE_INSTALL_RULES "Generate KSpaceJet install rules." ON)
set(KSJ_ENABLE_INSTALL_RPATH
    OFF
    CACHE BOOL "" FORCE)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(KSpaceJetBootstrap)
include(CTest)
include(KSpaceJetBuildSupport)
include(KSpaceJetStaticAnalysis)

# The type registry is authored once and its public C++/C headers are checked in.  Builds deliberately validate rather
# than regenerate them: a stale generated API must fail the build instead of silently changing an artifact under the
# developer's feet.
find_package(
  Python3
  COMPONENTS Interpreter
  REQUIRED)
add_custom_target(
  ksj_type_registry_check ALL
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/type_registry/generate.py" --project-root
          "${CMAKE_CURRENT_SOURCE_DIR}" --check
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/types/registry.json"
          "${CMAKE_CURRENT_SOURCE_DIR}/schemas/type-registry.schema.json"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/type_registry/generate.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/libs/recon/kspacejet-recon-model/include/kspacejet/recon/type_registry.hpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/libs/recon/kspacejet-provider-sdk/include/kspacejet/provider/type_registry.h"
  COMMENT "Validating checked-in KSpaceJet type-registry headers"
  VERBATIM)

if(NOT DEFINED KSJ_DEPENDENCY_PROVIDER OR NOT KSJ_DEPENDENCY_PROVIDER STREQUAL "conan")
  message(
    FATAL_ERROR
      "KSpaceJet requires Conan 2 dependencies. Export the local recipes, run `conan install . --output-folder=out/build/<preset> --build=missing`, then configure the matching CMake preset."
  )
endif()

ksj_use_flat_output_directories()
set(KSJ_INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}")
set(KSJ_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
set(KSJ_RUNTIME_DEPENDENCY_SET ksj_runtime_dependencies)
set(KSJ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(KSJ_INSTALL_BUNDLE_RUNTIME_DEPS
    OFF
    CACHE BOOL "Bundle runtime dependency libraries into the install tree.")
set(KSJ_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs")
set(KSJ_CORE_BASE_INCLUDE_DIR "${KSJ_LIBS_DIR}/core/kspacejet-base/include")
set(KSJ_CORE_CONFIG_INCLUDE_DIR "${KSJ_LIBS_DIR}/core/kspacejet-config/include")
set(KSJ_CORE_PLATFORM_INCLUDE_DIR "${KSJ_LIBS_DIR}/core/kspacejet-platform/include")
set(KSJ_CORE_PROCESS_RUNTIME_INCLUDE_DIR "${KSJ_LIBS_DIR}/core/kspacejet-process-runtime/include")
set(KSJ_CORE_CRASH_INCLUDE_DIR "${KSJ_LIBS_DIR}/core/kspacejet-crash/include")
set(KSJ_CORE_PERFORMANCE_INCLUDE_DIR "${KSJ_LIBS_DIR}/core/kspacejet-performance/include")
set(KSJ_CORE_PLATFORM_SOURCE_DIR "${KSJ_LIBS_DIR}/core/kspacejet-platform/src")
set(KSJ_NUMERICS_LIBS_DIR "${KSJ_LIBS_DIR}/numerics/kspacejet-numerics")
set(KSJ_NUMERICS_INCLUDE_DIR "${KSJ_NUMERICS_LIBS_DIR}/include")
set(KSJ_MATIO_DIR "${KSJ_LIBS_DIR}/io/kspacejet-matio")
set(KSJ_NUFFT_DIR "${KSJ_LIBS_DIR}/numerics/kspacejet-nufft")
set(KSJ_MRI_DEBUG_DIR "${KSJ_LIBS_DIR}/mri/kspacejet-mri-debug")
set(KSJ_MRI_DEBUG_INCLUDE_DIR "${KSJ_MRI_DEBUG_DIR}/include")

set(KSJ_PERFORMANCE_LEVEL
    "0"
    CACHE STRING "KSpaceJet performance instrumentation level (0=off, 1=metrics, 2=metrics+trace)")
set_property(CACHE KSJ_PERFORMANCE_LEVEL PROPERTY STRINGS 0 1 2)
option(KSJ_BUILD_UNIT_TESTS "Build tests/unit CTest targets from the root entry." OFF)
option(KSJ_BUILD_BENCHMARKS "Build tests/benchmarks executables from the root entry." OFF)
option(KSJ_BUILD_RESEARCH "Build tests/research executables from the root entry." OFF)
option(KSJ_BUILD_APPLICATIONS "Configure the KSpaceJet application targets." ON)
option(KSJ_ENABLE_NATIVE_ARCH_OPTIMIZATION
       "Enable target-local native CPU code generation for performance experiments." OFF)

set(KSJ_BUILD_TEST_PROJECTS OFF)
if(KSJ_BUILD_UNIT_TESTS
   OR KSJ_BUILD_BENCHMARKS
   OR KSJ_BUILD_RESEARCH)
  set(KSJ_BUILD_TEST_PROJECTS ON)
endif()

if(NOT KSJ_PERFORMANCE_LEVEL MATCHES "^[012]$")
  message(FATAL_ERROR "KSJ_PERFORMANCE_LEVEL must be 0, 1, or 2.")
endif()
if(KSJ_PERFORMANCE_LEVEL GREATER 0 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "KSJ_PERFORMANCE_LEVEL > 0 is supported only on Linux.")
endif()
if((KSJ_BUILD_BENCHMARKS OR KSJ_BUILD_RESEARCH) AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "KSpaceJet benchmark and research projects are supported only on Linux.")
endif()
if(KSJ_BUILD_TEST_PROJECTS
   AND NOT CMAKE_CONFIGURATION_TYPES
   AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  message(FATAL_ERROR "KSpaceJet tests projects are supported only with a Release build type.")
endif()
macro(ksj_add_core_library_targets)
  add_subdirectory(libs/core)
  add_subdirectory(libs/numerics)
endmacro()

macro(ksj_add_reconstruction_library_targets)
  add_subdirectory(libs/io/kspacejet-matio)
  add_subdirectory(libs/io/kspacejet-ismrmrd)
  add_subdirectory(libs/recon)
  add_subdirectory(libs/mri)
endmacro()

if(KSJ_BUILD_TEST_PROJECTS)
  if(KSJ_BUILD_APPLICATIONS)
    message(
      FATAL_ERROR
        "KSJ_BUILD_UNIT_TESTS, KSJ_BUILD_BENCHMARKS, and KSJ_BUILD_RESEARCH use a dedicated test configuration and cannot be combined with KSJ_BUILD_APPLICATIONS. Configure product applications and tests in separate build trees."
    )
  endif()

  include(KSpaceJetBuildTargets)
  enable_testing()

  # Test executables can load Intel runtime libraries through their KSpaceJet dependencies. Resolve the package at this
  # scope so KSJ_SET_EXECUTABLE_RUNTIME_PATH embeds the current runtime directories in every test executable's build
  # RPATH.
  find_package(Intel CONFIG REQUIRED)
  find_package(ISMRMRD CONFIG REQUIRED)
  find_package(matio CONFIG REQUIRED)
  find_package(nlohmann_json CONFIG REQUIRED)

  if(KSJ_BUILD_UNIT_TESTS OR KSJ_BUILD_BENCHMARKS)
    ksj_add_core_library_targets()
  endif()

  if(KSJ_BUILD_UNIT_TESTS)
    ksj_add_reconstruction_library_targets()
    add_subdirectory(providers)
  endif()

  add_subdirectory(tests)
  if(KSJ_ENABLE_INSTALL_RULES)
    ksj_install_thirdparty_runtime_dependencies(${KSJ_RUNTIME_DEPENDENCY_SET})
    ksj_install_intel_runtime_libraries()
  endif()
  return()
endif()

find_package(Boost CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)
find_package(Intel CONFIG REQUIRED)
find_package(
  ITK CONFIG REQUIRED
  COMPONENTS ITKCommon
             ITKLabelMap
             ITKBiasCorrection
             ITKSmoothing
             ITKStatistics
             ITKOptimizers
             ITKTransform)
find_package(matio CONFIG REQUIRED)
find_package(ISMRMRD CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

include(KSpaceJetBuildTargets)

set(KSJ_INTEL_TARGETS Intel::ippi Intel::ipps Intel::ippcore Intel::ippcv Intel::ippdc Intel::mkl_rt)
set(KSJ_ITK_TARGETS
    ITKCommon
    ITKLabelMap
    ITKBiasCorrection
    ITKSmoothing
    ITKStatistics
    ITKOptimizers
    ITKTransform)
set(KSJ_OPENCV_TARGETS opencv_core opencv_imgproc opencv_xphoto opencv_photo opencv_imgcodecs)

if(KSJ_PERFORMANCE_LEVEL GREATER 1)
  find_package(gperftools CONFIG REQUIRED)
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/numerics)
add_subdirectory(libs/io/kspacejet-matio)
add_subdirectory(libs/io/kspacejet-ismrmrd)
add_subdirectory(libs/recon)
add_subdirectory(libs/mri)
add_subdirectory(providers)

if(KSJ_BUILD_APPLICATIONS)
  add_subdirectory(apps)
  if(BUILD_TESTING)
    add_subdirectory(tests/apps)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  ksj_add_linux_static_memory_leak_check_target()
  ksj_add_numeric_dependency_boundary_check_target()
endif()

if(KSJ_ENABLE_INSTALL_RULES)
  ksj_install_thirdparty_runtime_dependencies(${KSJ_RUNTIME_DEPENDENCY_SET})
  ksj_install_intel_runtime_libraries()
endif()
