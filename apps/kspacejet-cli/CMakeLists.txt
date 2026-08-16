ksj_add_application(
  TARGET
  ksj_cli
  OUTPUT_NAME
  ksj
  SOURCES
  main.cpp
  provider_init.cpp
  INSTALL)

# `ksj pipeline validate` is deliberately a thin product entry point over the shared graph parser.  The CLI must not
# grow a second PipelineDefinition parser, nor should it reach into Provider loading or runtime scheduling.
target_link_libraries(ksj_cli PRIVATE KSpaceJet::recon_graph)

target_compile_definitions(
  ksj_cli PRIVATE KSJ_PROVIDER_TEMPLATE_INSTALL_DIR="${CMAKE_INSTALL_FULL_DATADIR}/kspacejet/templates/provider"
                  KSJ_PROVIDER_TEMPLATE_SOURCE_DIR="${KSJ_ROOT}/sdk/templates/provider")

if(KSJ_ENABLE_INSTALL_RULES)
  install(DIRECTORY "${KSJ_ROOT}/sdk/templates/provider/"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/kspacejet/templates/provider")
endif()
