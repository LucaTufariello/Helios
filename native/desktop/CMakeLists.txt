find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui GuiPrivate Widgets Network Concurrent ShaderTools Test)
qt_standard_project_setup()

add_library(GodEyeDesktopCore STATIC
  Core/Geography.cpp Core/Missions.cpp Core/Products.cpp)
target_include_directories(GodEyeDesktopCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(GodEyeDesktopCore PUBLIC GodEyeSpatial Qt6::Core Qt6::Gui)
target_compile_features(GodEyeDesktopCore PUBLIC cxx_std_20)

qt_add_executable(GodEyeDesktop MACOSX_BUNDLE
  main.cpp Graphics/TileCache.cpp Graphics/MapWidget.cpp
  UI/MainWindow.cpp UI/MissionWindow.cpp UI/SatelliteWindow.cpp Validation.cpp)
target_link_libraries(GodEyeDesktop PRIVATE GodEyeDesktopCore Qt6::GuiPrivate
  Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
set_target_properties(GodEyeDesktop PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.godeye.desktop.preview"
  MACOSX_BUNDLE_BUNDLE_NAME "GodEye Desktop"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1")
qt_add_shaders(GodEyeDesktop "godeye_shaders"
  PREFIX "/godeye"
  FILES shaders/globe.vert shaders/globe.frag)

if(NOT MSVC)
  target_compile_options(GodEyeDesktopCore PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(GodEyeDesktop PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  qt_add_executable(GodEyeDesktopTests Tests/CoreTests.cpp)
  target_link_libraries(GodEyeDesktopTests PRIVATE GodEyeDesktopCore Qt6::Test)
  target_compile_definitions(GodEyeDesktopTests PRIVATE
    GODEYE_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Tests/GodEyeCoreTests/Fixtures")
  add_test(NAME desktop-core COMMAND GodEyeDesktopTests)
endif()

install(TARGETS GodEyeDesktop BUNDLE DESTINATION . RUNTIME DESTINATION bin)
