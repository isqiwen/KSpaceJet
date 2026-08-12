ksj_add_application(
  TARGET
  ksj_cli
  OUTPUT_NAME
  ksj
  SOURCES
  main.cpp
  INSTALL)

# `ksj pipeline validate` is deliberately a thin product entry point over the
# shared graph parser.  The CLI must not grow a second PipelineDefinition
# parser, nor should it reach into Provider loading or runtime scheduling.
target_link_libraries(ksj_cli PRIVATE KSpaceJet::recon_graph)
