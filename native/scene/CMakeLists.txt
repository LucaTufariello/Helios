set(GODEYE_CESIUM_SOURCE "${PROJECT_SOURCE_DIR}/output/vendor/cesium-native" CACHE PATH
    "Pinned Cesium Native source prepared by native/desktop/build.sh")
if(NOT EXISTS "${GODEYE_CESIUM_SOURCE}/CMakeLists.txt")
  message(FATAL_ERROR "Run native/desktop/build.sh to prepare the pinned native scene dependency")
endif()
execute_process(COMMAND git -C "${GODEYE_CESIUM_SOURCE}" rev-parse HEAD
  OUTPUT_VARIABLE godeye_cesium_revision OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT godeye_cesium_revision STREQUAL "1bca8021711294cc83a5a032599ac72d9b3166c9")
  message(FATAL_ERROR "Unexpected Cesium Native revision; GodEye requires pinned v0.65.0")
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
add_subdirectory("${GODEYE_CESIUM_SOURCE}" "${CMAKE_BINARY_DIR}/cesium" EXCLUDE_FROM_ALL)
target_compile_definitions(CesiumImage PRIVATE STBI_MAX_DIMENSIONS=8192)
add_library(GodEyeScene STATIC Scene.cpp SceneTransportQt.cpp)
target_include_directories(GodEyeScene PUBLIC include)
target_compile_features(GodEyeScene PUBLIC cxx_std_20)
target_link_libraries(GodEyeScene PRIVATE Cesium3DTilesSelection CesiumGltfContent
  Cesium3DTilesContent Qt6::Core Qt6::Network)

# Keep the licenses accompanying the actual pinned static dependencies in the
# deployed application. Source licensing does not replace dependency notices.
# Cesium's imported dependency targets are directory-scoped. Its cached fmt
# config directory identifies the matching installed triplet without promoting
# third-party targets into the parent project.
get_filename_component(godeye_dependency_root "${fmt_DIR}/../.." ABSOLUTE)
file(READ "${GODEYE_CESIUM_SOURCE}/LICENSE" godeye_cesium_license)
set(godeye_scene_notices "${CMAKE_BINARY_DIR}/SCENE-NOTICES.txt")
file(WRITE "${godeye_scene_notices}" "GodEye native scene components\nCesium Native v0.65.0, commit ${godeye_cesium_revision}\n${godeye_cesium_license}\n")
file(GLOB godeye_dependency_licenses "${godeye_dependency_root}/share/*/copyright")
foreach(godeye_license IN LISTS godeye_dependency_licenses)
  file(READ "${godeye_license}" godeye_license_text)
  get_filename_component(godeye_component_dir "${godeye_license}" DIRECTORY)
  get_filename_component(godeye_component "${godeye_component_dir}" NAME)
  file(APPEND "${godeye_scene_notices}" "\n--- ${godeye_component} ---\n${godeye_license_text}\n")
endforeach()
if(NOT godeye_dependency_licenses)
  message(FATAL_ERROR "Cannot locate the scene dependency copyright notices")
endif()
