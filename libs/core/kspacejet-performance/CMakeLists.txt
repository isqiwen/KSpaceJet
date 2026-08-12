set(KSJ_PERFORMANCE_SOURCES src/lightweight_scan_timing.cpp)

if(KSJ_PERFORMANCE_LEVEL GREATER 0)
  list(APPEND KSJ_PERFORMANCE_SOURCES src/analysis.cpp)
else()
  list(APPEND KSJ_PERFORMANCE_SOURCES src/analysis_noop.cpp)
endif()

ksj_add_core_component(performance INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include" SOURCES ${KSJ_PERFORMANCE_SOURCES})

target_compile_definitions(ksj_core PUBLIC KSJ_PERFORMANCE_LEVEL=${KSJ_PERFORMANCE_LEVEL})

if(KSJ_PERFORMANCE_LEVEL GREATER 1)
  target_link_libraries(ksj_core PRIVATE gperftools::profiler)
endif()
