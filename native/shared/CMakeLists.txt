add_library(GodEyeSpatial STATIC Sources/Spatial.cpp)
target_include_directories(GodEyeSpatial PUBLIC Sources/include)
target_compile_features(GodEyeSpatial PUBLIC cxx_std_20)
set_target_properties(GodEyeSpatial PROPERTIES
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(GodEyeSpatial PRIVATE /W4)
else()
  target_compile_options(GodEyeSpatial PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  add_executable(GodEyeSpatialTests Tests/SpatialTests.cpp Tests/CHeaderTest.c)
  target_link_libraries(GodEyeSpatialTests PRIVATE GodEyeSpatial)
  add_test(NAME spatial COMMAND GodEyeSpatialTests)
endif()
