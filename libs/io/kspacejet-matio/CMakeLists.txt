ksj_include_project_bootstrap()

find_package(matio CONFIG REQUIRED)

if(TARGET ksj_matio_headers)
  target_link_libraries(ksj_matio_headers INTERFACE KSpaceJet::array KSpaceJet::sparse matio::matio)
endif()

add_library(ksj_matio INTERFACE)
add_library(KSpaceJet::matio ALIAS ksj_matio)
target_compile_features(ksj_matio INTERFACE cxx_std_20)
target_link_libraries(ksj_matio INTERFACE KSpaceJet::matio_headers)
