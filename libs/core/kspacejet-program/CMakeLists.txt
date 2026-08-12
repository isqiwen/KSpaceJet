add_library(ksj_program STATIC EXCLUDE_FROM_ALL src/program.cpp)

ksj_target_enable_defaults(ksj_program)
target_include_directories(ksj_program PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(ksj_program PUBLIC KSpaceJet::core)
set_target_properties(ksj_program PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "libs/core")
