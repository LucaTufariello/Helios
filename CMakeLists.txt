cmake_minimum_required(VERSION 3.24)
project(HeliosShared LANGUAGES C CXX)

include(CTest)
add_subdirectory(native/shared)

option(HELIOS_BUILD_DESKTOP "Build the Qt desktop client" OFF)
if(HELIOS_BUILD_DESKTOP)
  add_subdirectory(native/desktop)
endif()
