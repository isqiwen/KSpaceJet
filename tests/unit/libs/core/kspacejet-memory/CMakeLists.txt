ksj_add_gtest(
  ksj_memory_tests
  MODULE
  core.kspacejet-memory
  SOURCES
  memory_pool_tests.cpp
  memory_pool_stress_tests.cpp
  LINK_LIBS
  KSpaceJet::core
  LABELS
  core
  kspacejet-memory)

target_compile_definitions(ksj_memory_tests PRIVATE KSJ_MEMORY_ENABLE_TEST_ACCESS=1)
