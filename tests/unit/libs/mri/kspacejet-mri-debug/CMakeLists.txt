ksj_add_gtest(
  ksj_mri_debug_tests
  MODULE
  mri.kspacejet-mri-debug
  SOURCES
  array_analysis_tests.cpp
  array_dump_tests.cpp
  "${CMAKE_SOURCE_DIR}/tests/unit/libs/numerics/test_memory_broker.cpp"
  LINK_LIBS
  KSpaceJet::mri_debug
  KSpaceJet::numerics_runtime
  KSpaceJet::matio_headers
  matio::matio
  LABELS
  mri
  kspacejet-mri-debug)

ksj_set_executable_runtime_path(ksj_mri_debug_tests)
