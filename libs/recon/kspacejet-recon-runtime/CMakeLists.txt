ksj_include_project_bootstrap()

set(PROJ_NAME ksj_recon_runtime)

add_library(
  ${PROJ_NAME} SHARED
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/acquisition_classification.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/buffer_pool.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/calibration_gate.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/bounded_edge.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/cartesian_frame_slot.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/detail/slab_range_claim.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/fixed_reorder_buffer.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/fixed_key_slot_table.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/fixed_buffer_edge.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/host_frame_assembler.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/ismrmrd_hdf5_replay.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/key_shard.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/m3_reorder_ingress.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/plan_bound_data_plane.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/resource_ledger.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/resource_vector_ledger.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/reorder_buffer.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/scan_lifecycle.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/serial_cartesian_pipeline.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/runtime/synchronous_firing_lease.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/acquisition_classification.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/buffer_pool.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/calibration_gate.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/cartesian_frame_slot.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/fixed_reorder_buffer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/fixed_key_slot_table.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/fixed_buffer_edge.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/host_frame_assembler.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ismrmrd_hdf5_replay.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/key_shard.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/m3_reorder_ingress.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/plan_bound_data_plane.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/resource_ledger.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/resource_vector_ledger.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/scan_lifecycle.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/serial_cartesian_pipeline.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/slab_range_claim.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/synchronous_firing_lease.cpp")
add_library(KSpaceJet::recon_runtime ALIAS ${PROJ_NAME})

target_compile_features(${PROJ_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJ_NAME} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options KSpaceJet::ismrmrd ISMRMRD::ISMRMRD
  PUBLIC KSpaceJet::core KSpaceJet::provider_loader KSpaceJet::recon_contracts)

set_target_properties(
  ${PROJ_NAME}
  PROPERTIES CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET default
             VISIBILITY_INLINES_HIDDEN OFF
             WINDOWS_EXPORT_ALL_SYMBOLS ON)

ksj_set_library_runtime_path(${PROJ_NAME})
ksj_install_target(${PROJ_NAME})
