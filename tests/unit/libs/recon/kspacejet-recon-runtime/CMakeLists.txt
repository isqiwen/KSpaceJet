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
  calibration_artifact_store_tests.cpp
  cartesian_rss_hdf5_tests.cpp
  noncartesian_rss_hdf5_tests.cpp
  radial_rss_hdf5_tests.cpp
  fixed_buffer_edge_tests.cpp
  frame_slot_tests.cpp
  ismrmrd_hdf5_replay_tests.cpp
  ismrmrd_semantic_ingress_tests.cpp
  provider_node_instance_tests.cpp
  resource_vector_ledger_tests.cpp
  runtime_primitives_tests.cpp
  serial_cartesian_pipeline_tests.cpp
  synchronous_graph_executor_tests.cpp
  synchronous_firing_lease_tests.cpp
  LINK_LIBS
  KSpaceJet::recon_runtime
  KSpaceJet::nufft
  KSpaceJet::ismrmrd
  ISMRMRD::ISMRMRD
  LABELS
  recon
  runtime)

add_dependencies(
  ksj_recon_runtime_tests
  ksj_cartesian_recon_provider
  ksj_noncartesian_recon_provider
  ksj_coil_combine_provider
  ksj_calibration_provider
  ksj_kspace_conditioning_provider
  ksj_synchronous_firing_lease_test_provider)
target_compile_definitions(
  ksj_recon_runtime_tests
  PRIVATE
    KSJ_CARTESIAN_RECON_PROVIDER_MODULE="$<TARGET_FILE:ksj_cartesian_recon_provider>"
    KSJ_NONCARTESIAN_RECON_PROVIDER_MODULE="$<TARGET_FILE:ksj_noncartesian_recon_provider>"
    KSJ_COIL_COMBINE_PROVIDER_MODULE="$<TARGET_FILE:ksj_coil_combine_provider>"
    KSJ_CARTESIAN_RECON_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-cartesian-recon/contracts/cartesian_ifft2_coil_images.json"
    KSJ_NONCARTESIAN_RECON_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-noncartesian-recon/contracts/noncartesian_adjoint_reconstruct.json"
    KSJ_RADIAL_GRIDDING_RECON_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-noncartesian-recon/contracts/radial_gridding_reconstruct.json"
    KSJ_COIL_COMBINE_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-coil-combine/contracts/coil_combine_rss.json"
    KSJ_CALIBRATION_PROVIDER_MODULE="$<TARGET_FILE:ksj_calibration_provider>"
    KSJ_KSPACE_CONDITIONING_PROVIDER_MODULE="$<TARGET_FILE:ksj_kspace_conditioning_provider>"
    KSJ_NOISE_MODEL_ESTIMATE_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-calibration/contracts/noise_model_estimate.json"
    KSJ_PHASE_CORRECTION_ESTIMATE_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-calibration/contracts/phase_correction_estimate.json"
    KSJ_COIL_COMPRESSION_BASIS_ESTIMATE_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-calibration/contracts/coil_compression_basis_estimate.json"
    KSJ_NOISE_PREWHITEN_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-kspace-conditioning/contracts/noise_prewhiten.json"
    KSJ_PHASE_CORRECT_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-kspace-conditioning/contracts/phase_correct.json"
    KSJ_COIL_COMPRESS_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-kspace-conditioning/contracts/coil_compress.json"
    KSJ_READOUT_OVERSAMPLING_REMOVE_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-kspace-conditioning/contracts/readout_oversampling_remove.json"
    KSJ_SYNCHRONOUS_FIRING_LEASE_TEST_PROVIDER_MODULE="$<TARGET_FILE:ksj_synchronous_firing_lease_test_provider>")

# Keep the runtime-owned ISMRMRD boundary executable independently from the route suite. The latter also validates
# numerical Provider paths and currently has an unrelated Windows linalg import-library limitation; these focused
# source/sink tests depend only on recon-runtime and the small test Provider.
ksj_add_gtest(
  ksj_recon_runtime_ismrmrd_io_tests
  MODULE
  recon.kspacejet-recon-runtime
  SOURCES
  ismrmrd_hdf5_replay_tests.cpp
  synchronous_graph_executor_tests.cpp
  LINK_LIBS
  KSpaceJet::recon_runtime
  KSpaceJet::ismrmrd
  ISMRMRD::ISMRMRD
  LABELS
  recon
  runtime
  ismrmrd
  io)

add_dependencies(ksj_recon_runtime_ismrmrd_io_tests ksj_synchronous_firing_lease_test_provider)
target_compile_definitions(
  ksj_recon_runtime_ismrmrd_io_tests
  PRIVATE KSJ_SYNCHRONOUS_FIRING_LEASE_TEST_PROVIDER_MODULE="$<TARGET_FILE:ksj_synchronous_firing_lease_test_provider>")
set_tests_properties(
  recon.kspacejet-recon-runtime.ksj_recon_runtime_ismrmrd_io_tests
  PROPERTIES ENVIRONMENT
             "GTEST_FILTER=KSpaceJetIsmrmrdHdf5ReplaySource.*:SynchronousGraphExecutor.IsmrmrdImageArtifactSink*")

# Keep one positive end-to-end ISMRMRD artifact route for each supported reference geometry executable without requiring
# optional calibration and conditioning Providers. The full route suite remains the authoritative coverage for those
# branches once the Windows linalg import-library issue is resolved.
ksj_add_gtest(
  ksj_recon_runtime_ismrmrd_route_tests
  MODULE
  recon.kspacejet-recon-runtime
  SOURCES
  cartesian_rss_hdf5_tests.cpp
  noncartesian_rss_hdf5_tests.cpp
  radial_rss_hdf5_tests.cpp
  LINK_LIBS
  KSpaceJet::recon_runtime
  KSpaceJet::nufft
  KSpaceJet::ismrmrd
  ISMRMRD::ISMRMRD
  LABELS
  recon
  runtime
  ismrmrd
  io
  route)

add_dependencies(ksj_recon_runtime_ismrmrd_route_tests ksj_cartesian_recon_provider ksj_noncartesian_recon_provider
                 ksj_coil_combine_provider)
target_compile_definitions(
  ksj_recon_runtime_ismrmrd_route_tests
  PRIVATE
    KSJ_CARTESIAN_RECON_PROVIDER_MODULE="$<TARGET_FILE:ksj_cartesian_recon_provider>"
    KSJ_NONCARTESIAN_RECON_PROVIDER_MODULE="$<TARGET_FILE:ksj_noncartesian_recon_provider>"
    KSJ_COIL_COMBINE_PROVIDER_MODULE="$<TARGET_FILE:ksj_coil_combine_provider>"
    KSJ_CARTESIAN_RECON_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-cartesian-recon/contracts/cartesian_ifft2_coil_images.json"
    KSJ_NONCARTESIAN_RECON_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-noncartesian-recon/contracts/noncartesian_adjoint_reconstruct.json"
    KSJ_RADIAL_GRIDDING_RECON_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-noncartesian-recon/contracts/radial_gridding_reconstruct.json"
    KSJ_COIL_COMBINE_OPERATOR_CONTRACT="${CMAKE_SOURCE_DIR}/providers/kspacejet-coil-combine/contracts/coil_combine_rss.json"
    KSJ_CALIBRATION_PROVIDER_MODULE="not-selected"
    KSJ_KSPACE_CONDITIONING_PROVIDER_MODULE="not-selected"
    KSJ_NOISE_MODEL_ESTIMATE_OPERATOR_CONTRACT="not-selected"
    KSJ_PHASE_CORRECTION_ESTIMATE_OPERATOR_CONTRACT="not-selected"
    KSJ_COIL_COMPRESSION_BASIS_ESTIMATE_OPERATOR_CONTRACT="not-selected"
    KSJ_NOISE_PREWHITEN_OPERATOR_CONTRACT="not-selected"
    KSJ_PHASE_CORRECT_OPERATOR_CONTRACT="not-selected"
    KSJ_COIL_COMPRESS_OPERATOR_CONTRACT="not-selected"
    KSJ_READOUT_OVERSAMPLING_REMOVE_OPERATOR_CONTRACT="not-selected")
set_tests_properties(
  recon.kspacejet-recon-runtime.ksj_recon_runtime_ismrmrd_route_tests
  PROPERTIES
    ENVIRONMENT
    "GTEST_FILTER=CartesianRssHdf5Reconstruction.ReconstructsMultiCoilRssThroughTheGenericGraph:NoncartesianRssHdf5Reconstruction.ReconstructsMultiCoilRssFromActualTrajectoryThroughTheGenericGraph:RadialRssHdf5Reconstruction.RunsTheExplicitAnalyticRampGriddingRouteForCyclesPerFovInput"
)
