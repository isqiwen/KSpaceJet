ksj_include_project_bootstrap()

if(NOT KSJ_BUILD_APPLICATIONS)
  return()
endif()

find_package(CLI11 CONFIG REQUIRED)

function(ksj_add_application)
  set(options INSTALL)
  set(one_value_args TARGET OUTPUT_NAME)
  set(multi_value_args SOURCES)
  cmake_parse_arguments(KSJ_APP "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT KSJ_APP_TARGET
     OR NOT KSJ_APP_OUTPUT_NAME
     OR NOT KSJ_APP_SOURCES)
    message(FATAL_ERROR "ksj_add_application() requires TARGET, OUTPUT_NAME, and SOURCES.")
  endif()

  add_executable(${KSJ_APP_TARGET} ${KSJ_APP_SOURCES})
  ksj_target_enable_defaults(${KSJ_APP_TARGET})
  target_link_libraries(${KSJ_APP_TARGET} PRIVATE CLI11::CLI11 KSpaceJet::core KSpaceJet::platform_runtime_libraries
                                                  KSpaceJet::executable_link_options)
  set_target_properties(${KSJ_APP_TARGET} PROPERTIES OUTPUT_NAME "${KSJ_APP_OUTPUT_NAME}" FOLDER "apps")
  ksj_set_executable_runtime_path(${KSJ_APP_TARGET})
  ksj_stage_thirdparty_runtime_dlls(${KSJ_APP_TARGET})

  if(KSJ_APP_INSTALL)
    ksj_install_target(${KSJ_APP_TARGET})
  endif()
endfunction()

add_subdirectory(kspacejet-cli)
add_subdirectory(kspacejet-gateway)
add_subdirectory(kspacejet-recon)
add_subdirectory(kspacejet-research)
