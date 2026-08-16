ksj_add_gtest(
  ksj_noncartesian_recon_provider_tests
  MODULE
  providers.kspacejet-noncartesian-recon
  SOURCES
  noncartesian_recon_provider_tests.cpp
  ../../libs/numerics/test_memory_broker.cpp
  LINK_LIBS
  KSpaceJet::provider_loader
  KSpaceJet::core
  KSpaceJet::nufft
  KSpaceJet::numerics_runtime
  LABELS
  providers
  provider-loader)

add_dependencies(ksj_noncartesian_recon_provider_tests ksj_noncartesian_recon_provider)
target_compile_definitions(
  ksj_noncartesian_recon_provider_tests
  PRIVATE KSJ_NONCARTESIAN_RECON_PROVIDER_MODULE="$<TARGET_FILE:ksj_noncartesian_recon_provider>")
