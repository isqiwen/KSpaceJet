ksj_add_gtest(
  ksj_reference_cartesian_provider_tests
  MODULE
  providers.kspacejet-reference-cartesian
  SOURCES
  reference_cartesian_provider_tests.cpp
  LINK_LIBS
  KSpaceJet::provider_loader
  LABELS
  providers
  provider-loader)

add_dependencies(ksj_reference_cartesian_provider_tests ksj_reference_cartesian_provider)
target_compile_definitions(
  ksj_reference_cartesian_provider_tests
  PRIVATE KSJ_REFERENCE_CARTESIAN_PROVIDER_MODULE="$<TARGET_FILE:ksj_reference_cartesian_provider>")
