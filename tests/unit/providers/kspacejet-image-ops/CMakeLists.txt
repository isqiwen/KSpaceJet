ksj_add_gtest(
  ksj_image_ops_provider_tests
  MODULE
  providers.kspacejet-image-ops
  SOURCES
  image_ops_provider_tests.cpp
  LINK_LIBS
  KSpaceJet::provider_loader
  LABELS
  providers
  provider-loader)

add_dependencies(ksj_image_ops_provider_tests ksj_image_ops_provider)
target_compile_definitions(ksj_image_ops_provider_tests
                           PRIVATE KSJ_IMAGE_OPS_PROVIDER_MODULE="$<TARGET_FILE:ksj_image_ops_provider>")
