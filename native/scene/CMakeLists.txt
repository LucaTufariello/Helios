set(HELIOS_CESIUM_SOURCE "${PROJECT_SOURCE_DIR}/output/vendor/cesium-native" CACHE PATH
    "Pinned Cesium Native source prepared by native/desktop/build.sh")
if(NOT EXISTS "${HELIOS_CESIUM_SOURCE}/CMakeLists.txt")
  message(FATAL_ERROR "Run native/desktop/build.sh to prepare the pinned native scene dependency")
endif()
execute_process(COMMAND git -C "${HELIOS_CESIUM_SOURCE}" rev-parse HEAD
  OUTPUT_VARIABLE helios_cesium_revision OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT helios_cesium_revision STREQUAL "1bca8021711294cc83a5a032599ac72d9b3166c9")
  message(FATAL_ERROR "Unexpected Cesium Native revision; Helios requires pinned v0.65.0")
endif()
set(CESIUM_TESTS_ENABLED OFF CACHE BOOL "" FORCE)
set(CESIUM_DISABLE_CURL ON CACHE BOOL "" FORCE)
set(CESIUM_DISABLE_LIBJPEG_TURBO ON CACHE BOOL "" FORCE)
set(CESIUM_ENABLE_CLANG_TIDY OFF CACHE BOOL "" FORCE)
if(NOT EZVCPKG_BASEDIR)
  if(DEFINED ENV{EZVCPKG_BASEDIR})
    set(EZVCPKG_BASEDIR "$ENV{EZVCPKG_BASEDIR}" CACHE PATH "Pinned scene dependency cache")
  else()
    set(EZVCPKG_BASEDIR "${PROJECT_SOURCE_DIR}/output/vendor/vcpkg" CACHE PATH "Pinned scene dependency cache")
  endif()
endif()
add_subdirectory("${HELIOS_CESIUM_SOURCE}" "${CMAKE_BINARY_DIR}/cesium" EXCLUDE_FROM_ALL)
target_compile_definitions(CesiumImage PRIVATE STBI_MAX_DIMENSIONS=8192)
add_library(HeliosScene STATIC Scene.cpp SceneTransportQt.cpp)
target_include_directories(HeliosScene PUBLIC include)
target_compile_features(HeliosScene PUBLIC cxx_std_20)
target_link_libraries(HeliosScene PRIVATE Cesium3DTilesSelection CesiumGltfContent
  Cesium3DTilesContent Qt6::Core Qt6::Network)

# Keep the licenses accompanying the actual pinned static dependencies in the
# deployed application. Source licensing does not replace dependency notices.
# Cesium's imported dependency targets are directory-scoped. Its cached fmt
# config directory identifies the matching installed triplet without promoting
# third-party targets into the parent project.
get_filename_component(helios_dependency_root "${fmt_DIR}/../.." ABSOLUTE)
file(READ "${HELIOS_CESIUM_SOURCE}/LICENSE" helios_cesium_license)
set(helios_scene_notices "${CMAKE_BINARY_DIR}/SCENE-NOTICES.txt")
file(WRITE "${helios_scene_notices}" "Helios native scene components\nCesium Native v0.65.0, commit ${helios_cesium_revision}\n${helios_cesium_license}\n")
file(GLOB helios_dependency_licenses "${helios_dependency_root}/share/*/copyright")
foreach(helios_license IN LISTS helios_dependency_licenses)
  file(READ "${helios_license}" helios_license_text)
  get_filename_component(helios_component_dir "${helios_license}" DIRECTORY)
  get_filename_component(helios_component "${helios_component_dir}" NAME)
  file(APPEND "${helios_scene_notices}" "\n--- ${helios_component} ---\n${helios_license_text}\n")
endforeach()
if(NOT helios_dependency_licenses)
  message(FATAL_ERROR "Cannot locate the scene dependency copyright notices")
endif()
