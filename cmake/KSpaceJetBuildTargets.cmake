include_guard()

function(ksj_add_interface_target target_name alias_name)
  if(NOT TARGET ${target_name})
    add_library(${target_name} INTERFACE)
  endif()
  if(alias_name AND NOT TARGET ${alias_name})
    add_library(${alias_name} ALIAS ${target_name})
  endif()
endfunction()

ksj_add_interface_target(ksj_compile_options KSpaceJet::compile_options)
if(DEFINED PROJECT_OPTIONS_TARGET AND TARGET ${PROJECT_OPTIONS_TARGET})
  target_link_libraries(ksj_compile_options INTERFACE ${PROJECT_OPTIONS_TARGET})
endif()

target_compile_options(
  ksj_compile_options
  INTERFACE $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>>:-g>
            $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>>:-g>
            $<$<COMPILE_LANG_AND_ID:C,MSVC>:/MP> $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/MP>)

ksj_add_interface_target(ksj_platform_runtime_libraries KSpaceJet::platform_runtime_libraries)
if(WIN32)
  target_link_libraries(ksj_platform_runtime_libraries INTERFACE ws2_32 psapi)
else()
  target_link_libraries(ksj_platform_runtime_libraries INTERFACE m rt dl pthread)
endif()

ksj_add_interface_target(ksj_executable_link_options KSpaceJet::executable_link_options)
target_link_options(ksj_executable_link_options INTERFACE $<$<PLATFORM_ID:Linux>:-Wl,--disable-new-dtags>)

ksj_add_interface_target(ksj_shared_library_link_options KSpaceJet::shared_library_link_options)
target_link_options(ksj_shared_library_link_options INTERFACE $<$<PLATFORM_ID:Linux>:-Wl,--disable-new-dtags>
                    $<$<PLATFORM_ID:Linux>:-Wl,-z,defs>)

ksj_add_interface_target(ksj_platform_runtime_definitions KSpaceJet::platform_runtime_definitions)
if(NOT TARGET KSpaceJet::linux_runtime_definitions)
  add_library(KSpaceJet::linux_runtime_definitions ALIAS ksj_platform_runtime_definitions)
endif()
if(WIN32)
  target_compile_definitions(ksj_platform_runtime_definitions INTERFACE KSJ_BUILD_FOR_WINDOWS NOMINMAX
                                                                        WIN32_LEAN_AND_MEAN)
else()
  target_compile_definitions(ksj_platform_runtime_definitions INTERFACE KSJ_BUILD_FOR_LINUX KSJ_NO_BUSY_LOOP)
endif()

ksj_add_interface_target(ksj_intel_compat_definitions KSpaceJet::intel_compat_definitions)

ksj_add_interface_target(ksj_main_runtime_definitions KSpaceJet::main_runtime_definitions)
target_compile_definitions(ksj_main_runtime_definitions INTERFACE BUILDING_KSpaceJet)

ksj_add_interface_target(ksj_base_abi_definitions KSpaceJet::base_abi_definitions)
target_link_libraries(
  ksj_base_abi_definitions INTERFACE KSpaceJet::platform_runtime_definitions KSpaceJet::intel_compat_definitions
                                     KSpaceJet::main_runtime_definitions)
ksj_add_interface_target(ksj_matio_headers KSpaceJet::matio_headers)
target_include_directories(ksj_matio_headers INTERFACE "${KSJ_MATIO_DIR}/include")
