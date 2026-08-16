ksj_add_gtest(
  ksj_coil_combine_provider_tests
  MODULE
  providers.kspacejet-coil-combine
  SOURCES
  coil_combine_provider_tests.cpp
  LINK_LIBS
  KSpaceJet::provider_loader
  LABELS
  providers
  provider-loader)

add_dependencies(ksj_coil_combine_provider_tests ksj_coil_combine_provider)
target_compile_definitions(ksj_coil_combine_provider_tests
                           PRIVATE KSJ_COIL_COMBINE_PROVIDER_MODULE="$<TARGET_FILE:ksj_coil_combine_provider>")
