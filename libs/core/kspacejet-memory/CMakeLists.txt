set(_ksj_memory_public_link_libs)
set(_ksj_memory_have_libnuma 0)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(libnuma CONFIG REQUIRED)
  list(APPEND _ksj_memory_public_link_libs libnuma::libnuma)
  set(_ksj_memory_have_libnuma 1)
endif()

option(KSJ_MEMORY_ENABLE_STATS "Enable kspacejet-memory runtime stats collection." OFF)
option(KSJ_MEMORY_ENABLE_LEAK_TRACKING "Enable kspacejet-memory outstanding allocation tracking." OFF)

set(_ksj_memory_enable_stats 0)
if(KSJ_MEMORY_ENABLE_STATS)
  set(_ksj_memory_enable_stats 1)
endif()

set(_ksj_memory_enable_numa_diagnostics 0)
if(KSJ_MEMORY_ENABLE_STATS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_ksj_memory_enable_numa_diagnostics 1)
endif()

set(_ksj_memory_enable_leak_tracking 0)
if(KSJ_MEMORY_ENABLE_LEAK_TRACKING)
  set(_ksj_memory_enable_leak_tracking 1)
endif()

ksj_add_core_component(
  memory
  INCLUDE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  SOURCES
  src/bitset_slab.cpp
  src/memory_broker.cpp
  src/memory_lease.cpp
  src/memory_pool.cpp
  src/memory_space.cpp
  src/placement.cpp
  src/stats_formatter.cpp
  src/topology.cpp
  PUBLIC_LINK_LIBS
  ${_ksj_memory_public_link_libs})

target_compile_definitions(
  ksj_core
  PUBLIC KSJ_MEMORY_HAVE_LIBNUMA=${_ksj_memory_have_libnuma} KSJ_MEMORY_ENABLE_STATS=${_ksj_memory_enable_stats}
         KSJ_MEMORY_ENABLE_NUMA_DIAGNOSTICS=${_ksj_memory_enable_numa_diagnostics}
         KSJ_MEMORY_ENABLE_LEAK_TRACKING=${_ksj_memory_enable_leak_tracking})
