ksj_include_project_bootstrap()

add_subdirectory(kspacejet-array)
add_subdirectory(kspacejet-linalg)
add_subdirectory(kspacejet-fft)
add_subdirectory(kspacejet-signal)
add_subdirectory(kspacejet-image)
add_subdirectory(kspacejet-stats)
add_subdirectory(kspacejet-optimization)
add_subdirectory(kspacejet-special)
add_subdirectory(kspacejet-sparse)
add_subdirectory(kspacejet-numerics)
add_subdirectory(kspacejet-nufft)

if(TARGET KSpaceJet::numerics_runtime)
  foreach(
    KSJ_INTEL_NUMERICS_TARGET
    ksj_array
    ksj_linalg
    ksj_fft
    ksj_signal
    ksj_image
    ksj_sparse
    ksj_optimization)
    if(TARGET ${KSJ_INTEL_NUMERICS_TARGET})
      get_target_property(KSJ_INTEL_NUMERICS_TARGET_TYPE ${KSJ_INTEL_NUMERICS_TARGET} TYPE)
      if(KSJ_INTEL_NUMERICS_TARGET_TYPE STREQUAL "INTERFACE_LIBRARY")
        target_link_libraries(${KSJ_INTEL_NUMERICS_TARGET} INTERFACE KSpaceJet::numerics_runtime)
      else()
        target_link_libraries(${KSJ_INTEL_NUMERICS_TARGET} PUBLIC KSpaceJet::numerics_runtime)
      endif()
    endif()
  endforeach()
endif()

if(TARGET ksj_numerics AND TARGET KSpaceJet::nufft)
  target_link_libraries(ksj_numerics INTERFACE KSpaceJet::nufft)
endif()
