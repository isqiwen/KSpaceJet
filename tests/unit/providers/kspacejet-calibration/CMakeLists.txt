ksj_add_gtest(
  ksj_calibration_provider_tests
  MODULE
  providers.kspacejet-calibration
  SOURCES
  calibration_provider_tests.cpp
  LINK_LIBS
  KSpaceJet::provider_loader
  KSpaceJet::core
  LABELS
  providers
  provider-loader)

add_dependencies(ksj_calibration_provider_tests ksj_calibration_provider)
target_compile_definitions(ksj_calibration_provider_tests
                           PRIVATE KSJ_CALIBRATION_PROVIDER_MODULE="$<TARGET_FILE:ksj_calibration_provider>")
