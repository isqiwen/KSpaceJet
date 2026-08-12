add_library(ksj_provider_loader_test_provider SHARED test_provider.cpp)
target_compile_features(ksj_provider_loader_test_provider PRIVATE cxx_std_20)
target_compile_definitions(ksj_provider_loader_test_provider PRIVATE KSJ_PROVIDER_BUILDING_PLUGIN=1)
target_link_libraries(
  ksj_provider_loader_test_provider
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options KSpaceJet::provider_sdk)
set_target_properties(
  ksj_provider_loader_test_provider
  PROPERTIES CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN ON
             WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_library(ksj_provider_loader_bad_abi_test_provider SHARED test_provider.cpp)
target_compile_features(ksj_provider_loader_bad_abi_test_provider PRIVATE cxx_std_20)
target_compile_definitions(
  ksj_provider_loader_bad_abi_test_provider
  PRIVATE KSJ_PROVIDER_BUILDING_PLUGIN=1 KSJ_PROVIDER_LOADER_TEST_BAD_ABI=1)
target_link_libraries(
  ksj_provider_loader_bad_abi_test_provider
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options KSpaceJet::provider_sdk)
set_target_properties(
  ksj_provider_loader_bad_abi_test_provider
  PROPERTIES CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN ON
             WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_library(ksj_provider_loader_missing_query_test_module SHARED missing_query_module.cpp)
target_compile_features(ksj_provider_loader_missing_query_test_module PRIVATE cxx_std_20)
target_link_libraries(
  ksj_provider_loader_missing_query_test_module
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options)
set_target_properties(
  ksj_provider_loader_missing_query_test_module
  PROPERTIES CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN ON
             WINDOWS_EXPORT_ALL_SYMBOLS ON)

ksj_add_gtest(
  ksj_provider_loader_tests
  MODULE
  recon.kspacejet-provider-loader
  SOURCES
  provider_loader_tests.cpp
  LINK_LIBS
  KSpaceJet::provider_loader
  LABELS
  recon
  provider-loader)
add_dependencies(
  ksj_provider_loader_tests
  ksj_provider_loader_test_provider
  ksj_provider_loader_bad_abi_test_provider
  ksj_provider_loader_missing_query_test_module)
target_compile_definitions(
  ksj_provider_loader_tests
  PRIVATE KSJ_PROVIDER_LOADER_VALID_MODULE="$<TARGET_FILE:ksj_provider_loader_test_provider>"
          KSJ_PROVIDER_LOADER_BAD_ABI_MODULE="$<TARGET_FILE:ksj_provider_loader_bad_abi_test_provider>"
          KSJ_PROVIDER_LOADER_MISSING_QUERY_MODULE="$<TARGET_FILE:ksj_provider_loader_missing_query_test_module>")
