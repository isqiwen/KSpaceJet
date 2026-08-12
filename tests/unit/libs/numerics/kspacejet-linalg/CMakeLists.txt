ksj_add_gtest(
  ksj_linalg_tests
  MODULE
  numerics.kspacejet-linalg
  SOURCES
  linalg_tests.cpp
  LINK_LIBS
  KSpaceJet::linalg
  KSpaceJet::stats
  LABELS
  numerics
  kspacejet-linalg)

find_package(Intel CONFIG REQUIRED)
target_link_libraries(ksj_linalg_tests PRIVATE Intel::headers Intel::mkl_rt)
