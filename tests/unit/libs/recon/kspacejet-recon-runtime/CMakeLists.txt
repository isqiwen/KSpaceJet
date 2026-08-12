add_library(ksj_synchronous_firing_lease_test_provider SHARED synchronous_firing_lease_test_provider.cpp)
target_compile_features(ksj_synchronous_firing_lease_test_provider PRIVATE cxx_std_20)
target_compile_definitions(ksj_synchronous_firing_lease_test_provider PRIVATE KSJ_PROVIDER_BUILDING_PLUGIN=1)
target_link_libraries(ksj_synchronous_firing_lease_test_provider
                      PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options KSpaceJet::provider_sdk)
set_target_properties(
  ksj_synchronous_firing_lease_test_provider
  PROPERTIES CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN ON
             WINDOWS_EXPORT_ALL_SYMBOLS ON)

ksj_add_gtest(
  ksj_recon_runtime_tests
  MODULE
  recon.kspacejet-recon-runtime
  SOURCES
  buffer_pool_tests.cpp
  fixed_buffer_edge_tests.cpp
  frame_slot_tests.cpp
  fixed_reorder_buffer_tests.cpp
  fixed_key_slot_table_tests.cpp
  host_frame_assembler_tests.cpp
  ismrmrd_hdf5_replay_tests.cpp
  m3_reorder_ingress_tests.cpp
  minimal_cartesian_plan_bound_e2e_tests.cpp
  plan_bound_data_plane_tests.cpp
  resource_vector_ledger_tests.cpp
  runtime_primitives_tests.cpp
  serial_cartesian_pipeline_tests.cpp
  synchronous_firing_lease_tests.cpp
  LINK_LIBS
  KSpaceJet::recon_runtime
  ISMRMRD::ISMRMRD
  LABELS
  recon
  runtime)

add_dependencies(ksj_recon_runtime_tests
                 ksj_minimal_cartesian_provider
                 ksj_reference_cartesian_provider
                 ksj_synchronous_firing_lease_test_provider)
target_compile_definitions(
  ksj_recon_runtime_tests
  PRIVATE KSJ_MINIMAL_CARTESIAN_PROVIDER_MODULE="$<TARGET_FILE:ksj_minimal_cartesian_provider>"
          KSJ_REFERENCE_CARTESIAN_PROVIDER_MODULE="$<TARGET_FILE:ksj_reference_cartesian_provider>"
          KSJ_SYNCHRONOUS_FIRING_LEASE_TEST_PROVIDER_MODULE="$<TARGET_FILE:ksj_synchronous_firing_lease_test_provider>")
