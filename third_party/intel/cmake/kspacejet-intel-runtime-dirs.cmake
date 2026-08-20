# Loaded by Conan's CMakeDeps generator after find_package(Intel). The recipe stages a deliberately small subset of
# oneAPI in this layout; publishing a payload with a different layout is a manifest/schema change, not a silent CMake
# fallback.
get_filename_component(_ksj_intel_package_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

if(WIN32)
  set(KSJ_INTEL_RUNTIME_DIRS
      "${_ksj_intel_package_root}/oneapi/ipp/2026.0/redist/intel64"
      "${_ksj_intel_package_root}/oneapi/mkl/2026.1/redist/intel64"
      "${_ksj_intel_package_root}/oneapi/compiler/2026.1/redist/intel64_win/compiler"
      CACHE INTERNAL "Bundled Intel runtime directories exported by Conan")
else()
  set(KSJ_INTEL_RUNTIME_DIRS
      "${_ksj_intel_package_root}/oneapi/ipp/2026.0/lib" "${_ksj_intel_package_root}/oneapi/mkl/2026.1/lib"
      "${_ksj_intel_package_root}/oneapi/compiler/2026.1/lib"
      CACHE INTERNAL "Bundled Intel runtime directories exported by Conan")
endif()
