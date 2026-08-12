ksj_add_application(
  TARGET
  ksj_recon
  OUTPUT_NAME
  ksj-recon
  SOURCES
  main.cpp
  INSTALL)

# The reconstruction service package includes the small open reference
# Provider, but it does not link it: the service reaches every Provider only
# through the explicit trusted-path C ABI loader at runtime.
if(TARGET ksj_reference_cartesian_provider)
  add_dependencies(ksj_recon ksj_reference_cartesian_provider)
endif()
