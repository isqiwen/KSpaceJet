ksj_add_application(
  TARGET
  ksj_recon
  OUTPUT_NAME
  ksj-recon
  SOURCES
  main.cpp
  INSTALL)

target_link_libraries(ksj_recon PRIVATE KSpaceJet::recon_runtime)

# Every reconstruction command loads its Provider modules only through explicit caller-provided paths. These
# dependencies merely ensure a developer build produces the selectable modules alongside the executable; no module path
# is embedded into installed output.
if(TARGET ksj_cartesian_recon_provider)
  add_dependencies(ksj_recon ksj_cartesian_recon_provider)
endif()
if(TARGET ksj_coil_combine_provider)
  add_dependencies(ksj_recon ksj_coil_combine_provider)
endif()
if(TARGET ksj_noncartesian_recon_provider)
  add_dependencies(ksj_recon ksj_noncartesian_recon_provider)
endif()
if(TARGET ksj_calibration_provider)
  add_dependencies(ksj_recon ksj_calibration_provider)
endif()
if(TARGET ksj_kspace_conditioning_provider)
  add_dependencies(ksj_recon ksj_kspace_conditioning_provider)
endif()
