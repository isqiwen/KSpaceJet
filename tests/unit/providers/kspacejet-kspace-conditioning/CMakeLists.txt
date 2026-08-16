ksj_add_gtest(
  ksj_kspace_conditioning_provider_tests
  MODULE
  providers.kspacejet-kspace-conditioning
  SOURCES
  kspace_conditioning_provider_tests.cpp
  LINK_LIBS
  KSpaceJet::provider_loader
  LABELS
  providers
  provider-loader)

add_dependencies(ksj_kspace_conditioning_provider_tests ksj_kspace_conditioning_provider)
target_compile_definitions(
  ksj_kspace_conditioning_provider_tests
  PRIVATE KSJ_KSPACE_CONDITIONING_PROVIDER_MODULE="$<TARGET_FILE:ksj_kspace_conditioning_provider>")
