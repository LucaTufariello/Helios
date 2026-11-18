add_library(HeliosSpatial STATIC Sources/Spatial.cpp)
target_include_directories(HeliosSpatial PUBLIC Sources/include)
target_compile_features(HeliosSpatial PUBLIC cxx_std_20)
set_target_properties(HeliosSpatial PROPERTIES
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(HeliosSpatial PRIVATE /W4)
else()
  target_compile_options(HeliosSpatial PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  add_executable(HeliosSpatialTests Tests/SpatialTests.cpp Tests/CHeaderTest.c)
  target_link_libraries(HeliosSpatialTests PRIVATE HeliosSpatial)
  add_test(NAME spatial COMMAND HeliosSpatialTests)
endif()
