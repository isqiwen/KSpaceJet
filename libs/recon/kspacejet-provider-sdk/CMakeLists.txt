ksj_include_project_bootstrap()

# The Provider SDK is deliberately header-only: plugins invoke only their own exported query symbol and host-supplied
# capability callbacks.  Linking this target publishes the C ABI header without making the plugin depend on a C++
# runtime implementation or a private transport protocol.
set(PROJ_NAME ksj_provider_sdk)

add_library(${PROJ_NAME} INTERFACE)
add_library(KSpaceJet::provider_sdk ALIAS ${PROJ_NAME})
add_dependencies(${PROJ_NAME} ksj_type_registry_check)

target_compile_features(${PROJ_NAME} INTERFACE c_std_11)
target_include_directories(${PROJ_NAME} INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
                                                  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

# Keep a buildable, dependency-free ABI smoke target without turning the SDK into a runtime library.  It compiles the
# public header as both C11 and C++20 and remains excluded from ordinary product builds.
add_library(ksj_provider_sdk_header_check OBJECT EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/src/c_header_check.c"
                                                 "${CMAKE_CURRENT_LIST_DIR}/src/cxx_header_check.cpp")
target_link_libraries(ksj_provider_sdk_header_check PRIVATE KSpaceJet::provider_sdk)
target_compile_features(ksj_provider_sdk_header_check PRIVATE c_std_11 cxx_std_20)
add_dependencies(ksj_provider_sdk_header_check ksj_type_registry_check)

if(KSJ_ENABLE_INSTALL_RULES)
  install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
