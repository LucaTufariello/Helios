cmake_minimum_required(VERSION 3.24)
project(GodEyeShared LANGUAGES C CXX)

include(CTest)
add_subdirectory(native/shared)

option(GODEYE_BUILD_DESKTOP "Build the Qt desktop client" OFF)
if(GODEYE_BUILD_DESKTOP)
  add_subdirectory(native/desktop)
endif()
