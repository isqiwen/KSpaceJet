ksj_add_gtest(
  ksj_recon_graph_tests
  MODULE
  recon.kspacejet-recon-graph
  SOURCES
  artifact_json_tests.cpp
  pipeline_definition_tests.cpp
  LINK_LIBS
  KSpaceJet::recon_graph
  LABELS
  recon
  graph)

# Test data is intentionally maintained next to the recon test suite instead
# of being copied into a build tree, so schema and parser fixtures stay one
# inspectable source of truth.
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/../fixtures" KSJ_RECON_FIXTURE_DIR)
target_compile_definitions(
  ksj_recon_graph_tests
  PRIVATE KSJ_RECON_FIXTURE_DIR="${KSJ_RECON_FIXTURE_DIR}")
