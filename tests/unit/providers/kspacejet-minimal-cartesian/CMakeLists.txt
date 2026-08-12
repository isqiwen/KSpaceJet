ksj_add_gtest(
  ksj_minimal_cartesian_provider_tests
  MODULE
  providers.kspacejet-minimal-cartesian
  SOURCES
  minimal_cartesian_provider_tests.cpp
  LINK_LIBS
  KSpaceJet::provider_loader
  LABELS
  providers
  provider-loader)

add_dependencies(ksj_minimal_cartesian_provider_tests ksj_minimal_cartesian_provider)
target_compile_definitions(
  ksj_minimal_cartesian_provider_tests
  PRIVATE KSJ_MINIMAL_CARTESIAN_PROVIDER_MODULE="$<TARGET_FILE:ksj_minimal_cartesian_provider>")
