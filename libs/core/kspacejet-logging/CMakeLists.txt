if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR "Configure KSpaceJet from the repository root CMakeLists.txt.")
endif()

find_package(spdlog CONFIG REQUIRED)

ksj_add_core_component(
  logging
  INCLUDE_DIRS
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  SOURCES
  src/logging.cpp
  PUBLIC_LINK_LIBS
  spdlog::spdlog)
