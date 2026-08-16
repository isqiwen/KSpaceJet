ksj_add_gtest(
  ksj_cartesian_recon_provider_tests
  MODULE
  providers.kspacejet-cartesian-recon
  SOURCES
  cartesian_recon_provider_tests.cpp
  LINK_LIBS
  KSpaceJet::provider_loader
  KSpaceJet::core
  LABELS
  providers
  provider-loader)

add_dependencies(ksj_cartesian_recon_provider_tests ksj_cartesian_recon_provider)
target_compile_definitions(ksj_cartesian_recon_provider_tests
                           PRIVATE KSJ_CARTESIAN_RECON_PROVIDER_MODULE="$<TARGET_FILE:ksj_cartesian_recon_provider>")
