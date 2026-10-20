find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui GuiPrivate Widgets Network Concurrent ShaderTools Test)
qt_standard_project_setup()
add_executable(GodEyeWorkerHost WorkerHost.cpp)
target_compile_features(GodEyeWorkerHost PRIVATE cxx_std_20)
set_target_properties(GodEyeWorkerHost PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_subdirectory(../scene "${CMAKE_BINARY_DIR}/native/scene")

add_library(GodEyeDesktopCore STATIC
  Core/Geography.cpp Core/Investigations.cpp Core/Briefing.cpp Core/Missions.cpp Core/Monitoring.cpp Core/ScientificQueue.cpp Core/MonitoringRunner.cpp Core/Dossiers.cpp Core/Products.cpp Core/WorkerJob.cpp Core/OpticalCatalog.cpp Core/Credentials.cpp
  Core/HttpRequest.cpp Core/RadarCatalog.cpp Core/RadarDownload.cpp Core/JsonFile.cpp Core/Infrastructure.cpp Core/Timeline.cpp Core/Monitors.cpp Core/LibraryStorage.cpp Core/MapTools.cpp Core/Inference.cpp Core/DailyImagery.cpp
  Core/DomainFeeds.cpp Core/DomainParsers.cpp Core/DomainProto.cpp Core/DomainOrbit.cpp Core/DomainSocket.cpp Core/DomainNetwork.cpp Core/DomainHistory.cpp
  ../macos/Sources/CSGP4/GodEyeSGP4.cpp ../macos/Sources/CSGP4/SGP4.cpp)
target_include_directories(GodEyeDesktopCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/native/macos/Sources/CSGP4/include)
target_link_libraries(GodEyeDesktopCore PUBLIC GodEyeSpatial Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent)
target_compile_features(GodEyeDesktopCore PUBLIC cxx_std_20)
if(APPLE)
  enable_language(OBJCXX)
  target_sources(GodEyeDesktopCore PRIVATE Core/CredentialsMac.mm)
  target_compile_options(GodEyeDesktopCore PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
  target_link_libraries(GodEyeDesktopCore PRIVATE "-framework Security" "-framework Foundation" "-framework LocalAuthentication")
elseif(UNIX)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBSECRET QUIET libsecret-1)
    if(LIBSECRET_FOUND)
      target_compile_definitions(GodEyeDesktopCore PRIVATE GODEYE_HAVE_LIBSECRET=1)
      target_include_directories(GodEyeDesktopCore PRIVATE ${LIBSECRET_INCLUDE_DIRS})
    endif()
  endif()
endif()

qt_add_executable(GodEyeDesktop MACOSX_BUNDLE
  main.cpp Graphics/TileCache.cpp Graphics/MapWidget.cpp UI/WorkspaceStyle.cpp UI/WorkspacePanel.cpp
  UI/InvestigationWorkbench.cpp UI/SiteMonitoringPanel.cpp UI/MonitoringInboxPanel.cpp UI/DossierPanel.cpp UI/MainWindow.cpp UI/MissionWindow.cpp UI/MissionEditor.cpp UI/SatelliteWindow.cpp UI/ProcessingWindow.cpp UI/RadarPanel.cpp UI/ReviewWindow.cpp UI/TimelinePanel.cpp UI/MonitorPanel.cpp UI/MapToolsWindow.cpp UI/InferenceWindow.cpp UI/DomainWindow.cpp UI/CameraViewer.cpp Validation.cpp)
target_link_libraries(GodEyeDesktop PRIVATE GodEyeDesktopCore GodEyeScene Qt6::GuiPrivate
  Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
add_dependencies(GodEyeDesktop GodEyeWorkerHost)
set_target_properties(GodEyeDesktop PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/GodEyeInfo.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.godeye.desktop.preview"
  MACOSX_BUNDLE_BUNDLE_NAME "GodEye Desktop"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1")
qt_add_shaders(GodEyeDesktop "godeye_shaders"
  PREFIX "/godeye"
  FILES shaders/globe.vert shaders/globe.frag)
qt_add_resources(GodEyeDesktop "godeye_qa" PREFIX "/godeye" FILES Tests/review_fixture.py Tests/monitoring_fixture.py Tests/inference_fixture.py)

if(NOT MSVC)
  target_compile_options(GodEyeDesktopCore PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(GodEyeDesktop PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  qt_add_executable(GodEyeInvestigationTests Tests/InvestigationTests.cpp)
  target_link_libraries(GodEyeInvestigationTests PRIVATE GodEyeDesktopCore Qt6::Test)
  target_compile_definitions(GodEyeInvestigationTests PRIVATE GODEYE_TEST_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Tests/GodEyeCoreTests/Fixtures")
  add_test(NAME desktop-investigations COMMAND GodEyeInvestigationTests)
  set_tests_properties(desktop-investigations PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(GodEyeInvestigationUiTests Tests/InvestigationUITests.cpp UI/InvestigationWorkbench.cpp UI/MissionEditor.cpp UI/WorkspaceStyle.cpp)
  target_link_libraries(GodEyeInvestigationUiTests PRIVATE GodEyeDesktopCore Qt6::Widgets Qt6::Test)
  add_test(NAME desktop-investigations-ui COMMAND GodEyeInvestigationUiTests)
  set_tests_properties(desktop-investigations-ui PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  qt_add_executable(GodEyeMonitoringUiTests Tests/MonitoringUITests.cpp UI/SiteMonitoringPanel.cpp UI/MonitoringInboxPanel.cpp UI/WorkspaceStyle.cpp)
  target_link_libraries(GodEyeMonitoringUiTests PRIVATE GodEyeDesktopCore Qt6::Widgets Qt6::Test)
  add_test(NAME desktop-monitoring-ui COMMAND GodEyeMonitoringUiTests)
  set_tests_properties(desktop-monitoring-ui PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(GodEyeMonitoringTests Tests/MonitoringTests.cpp)
  target_link_libraries(GodEyeMonitoringTests PRIVATE GodEyeDesktopCore Qt6::Test)
  target_compile_definitions(GodEyeMonitoringTests PRIVATE GODEYE_TEST_PYTHON="${Python3_EXECUTABLE}" GODEYE_TEST_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/Tests/worker_fixture.py" GODEYE_TEST_HOST="$<TARGET_FILE:GodEyeWorkerHost>")
  add_dependencies(GodEyeMonitoringTests GodEyeWorkerHost)
  add_test(NAME desktop-monitoring COMMAND GodEyeMonitoringTests)
  set_tests_properties(desktop-monitoring PROPERTIES TIMEOUT 45)
  qt_add_executable(GodEyeDossierTests Tests/DossierTests.cpp)
  target_link_libraries(GodEyeDossierTests PRIVATE GodEyeDesktopCore Qt6::Test)
  add_test(NAME desktop-dossiers COMMAND GodEyeDossierTests)
  qt_add_executable(GodEyeDossierUiTests Tests/DossierUITests.cpp UI/DossierPanel.cpp UI/MissionEditor.cpp UI/WorkspaceStyle.cpp)
  target_include_directories(GodEyeDossierUiTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(GodEyeDossierUiTests PRIVATE GodEyeDesktopCore Qt6::Widgets Qt6::Concurrent Qt6::Test)
  add_test(NAME desktop-dossier-ui COMMAND GodEyeDossierUiTests)
  set_tests_properties(desktop-dossier-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(GodEyeWorkspaceUiTests Tests/WorkspaceUITests.cpp UI/WorkspacePanel.cpp UI/WorkspaceStyle.cpp)
  target_include_directories(GodEyeWorkspaceUiTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(GodEyeWorkspaceUiTests PRIVATE Qt6::Widgets Qt6::Test)
  add_test(NAME desktop-workspace-ui COMMAND GodEyeWorkspaceUiTests)
  set_tests_properties(desktop-workspace-ui PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(GodEyeDesktopTests Tests/CoreTests.cpp)
  target_link_libraries(GodEyeDesktopTests PRIVATE GodEyeDesktopCore Qt6::Test)
  target_compile_definitions(GodEyeDesktopTests PRIVATE
    GODEYE_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Tests/GodEyeCoreTests/Fixtures")
  add_test(NAME desktop-core COMMAND GodEyeDesktopTests)
  qt_add_executable(GodEyeSceneTests Tests/SceneTests.cpp)
  target_link_libraries(GodEyeSceneTests PRIVATE GodEyeScene Qt6::Test Qt6::Gui)
  add_test(NAME desktop-scene COMMAND GodEyeSceneTests)
  set_tests_properties(desktop-scene PROPERTIES TIMEOUT 60)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  qt_add_executable(GodEyeWorkerTests Tests/WorkerTests.cpp)
  target_link_libraries(GodEyeWorkerTests PRIVATE GodEyeDesktopCore Qt6::Test)
  target_compile_definitions(GodEyeWorkerTests PRIVATE GODEYE_TEST_PYTHON="${Python3_EXECUTABLE}"
    GODEYE_TEST_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/Tests/worker_fixture.py"
    GODEYE_TEST_WORKER_HOST="$<TARGET_FILE:GodEyeWorkerHost>")
  add_dependencies(GodEyeWorkerTests GodEyeWorkerHost)
  add_test(NAME desktop-worker COMMAND GodEyeWorkerTests)
  set_tests_properties(desktop-worker PROPERTIES TIMEOUT 60)
  qt_add_executable(GodEyeOpticalTests Tests/OpticalTests.cpp)
  target_link_libraries(GodEyeOpticalTests PRIVATE GodEyeDesktopCore Qt6::Test)
  target_compile_definitions(GodEyeOpticalTests PRIVATE
    GODEYE_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Tests/GodEyeCoreTests/Fixtures")
  add_test(NAME desktop-optical COMMAND GodEyeOpticalTests)
  qt_add_executable(GodEyeCredentialTests Tests/CredentialTests.cpp)
  target_link_libraries(GodEyeCredentialTests PRIVATE GodEyeDesktopCore Qt6::Test)
  add_test(NAME desktop-credentials COMMAND GodEyeCredentialTests)
  qt_add_executable(GodEyeRadarTests Tests/RadarTests.cpp)
  target_link_libraries(GodEyeRadarTests PRIVATE GodEyeDesktopCore Qt6::Test)
  add_test(NAME desktop-radar COMMAND GodEyeRadarTests)
  qt_add_executable(GodEyeDownloadTests Tests/DownloadTests.cpp)
  target_link_libraries(GodEyeDownloadTests PRIVATE GodEyeDesktopCore Qt6::Test)
  add_test(NAME desktop-download COMMAND GodEyeDownloadTests)
  set_tests_properties(desktop-download PROPERTIES TIMEOUT 30)
  qt_add_executable(GodEyeInfrastructureTests Tests/InfrastructureTests.cpp)
  target_link_libraries(GodEyeInfrastructureTests PRIVATE GodEyeDesktopCore Qt6::Test)
  target_compile_definitions(GodEyeInfrastructureTests PRIVATE
    GODEYE_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Tests/GodEyeCoreTests/Fixtures")
  add_test(NAME desktop-infrastructure COMMAND GodEyeInfrastructureTests)
  qt_add_executable(GodEyeMissionOperationsTests Tests/MissionOperationsTests.cpp)
  target_link_libraries(GodEyeMissionOperationsTests PRIVATE GodEyeDesktopCore Qt6::Test)
  add_test(NAME desktop-mission-operations COMMAND GodEyeMissionOperationsTests)
  qt_add_executable(GodEyeInferenceTests Tests/InferenceTests.cpp)
  target_link_libraries(GodEyeInferenceTests PRIVATE GodEyeDesktopCore Qt6::Test)
  target_compile_definitions(GodEyeInferenceTests PRIVATE
    GODEYE_INFERENCE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/inference_fixture.py"
    GODEYE_INFERENCE_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/Inference/infer.py"
    GODEYE_TEST_WORKER_HOST="$<TARGET_FILE:GodEyeWorkerHost>")
  add_dependencies(GodEyeInferenceTests GodEyeWorkerHost)
  add_test(NAME desktop-inference COMMAND GodEyeInferenceTests)
  set_tests_properties(desktop-inference PROPERTIES TIMEOUT 60)
  qt_add_executable(GodEyeInferenceUiTests Tests/InferenceUiTests.cpp UI/InferenceWindow.cpp UI/WorkspaceStyle.cpp)
  target_link_libraries(GodEyeInferenceUiTests PRIVATE GodEyeDesktopCore Qt6::Widgets Qt6::Concurrent Qt6::Test)
  target_compile_definitions(GodEyeInferenceUiTests PRIVATE
    GODEYE_INFERENCE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/inference_fixture.py")
  set_target_properties(GodEyeInferenceUiTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  add_dependencies(GodEyeInferenceUiTests GodEyeWorkerHost)
  add_test(NAME desktop-inference-ui COMMAND GodEyeInferenceUiTests)
  set_tests_properties(desktop-inference-ui PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(GodEyeDailyImageryTests Tests/DailyImageryTests.cpp)
  target_link_libraries(GodEyeDailyImageryTests PRIVATE GodEyeDesktopCore Qt6::Test)
  add_test(NAME desktop-daily-imagery COMMAND GodEyeDailyImageryTests)
  set_tests_properties(desktop-daily-imagery PROPERTIES TIMEOUT 60)
  qt_add_executable(GodEyeDomainTests Tests/DomainTests.cpp)
  target_link_libraries(GodEyeDomainTests PRIVATE GodEyeDesktopCore Qt6::Test)
  target_compile_definitions(GodEyeDomainTests PRIVATE
    GODEYE_DOMAIN_SOURCE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Sources/GodEyeCore/Resources"
    GODEYE_DOMAIN_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Tests/GodEyeCoreTests/Fixtures")
  add_test(NAME desktop-domain COMMAND GodEyeDomainTests)
  qt_add_executable(GodEyeDomainUiTests Tests/DomainUITests.cpp UI/DomainWindow.cpp UI/CameraViewer.cpp UI/WorkspaceStyle.cpp)
  target_link_libraries(GodEyeDomainUiTests PRIVATE GodEyeDesktopCore Qt6::Widgets Qt6::Test)
  set_target_properties(GodEyeDomainUiTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  add_test(NAME desktop-domain-ui COMMAND GodEyeDomainUiTests)
  set_tests_properties(desktop-domain-ui PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(GodEyeDomainLiveChecks Tests/DomainLiveChecks.cpp)
  target_link_libraries(GodEyeDomainLiveChecks PRIVATE GodEyeDesktopCore Qt6::Test)
  qt_add_executable(GodEyeMapToolsTests Tests/MapToolsTests.cpp)
  target_link_libraries(GodEyeMapToolsTests PRIVATE GodEyeDesktopCore Qt6::Test)
  add_test(NAME desktop-map-tools COMMAND GodEyeMapToolsTests)
  set_tests_properties(desktop-map-tools PROPERTIES TIMEOUT 100)
  qt_add_executable(GodEyeTileCacheTests Tests/TileCacheTests.cpp Graphics/TileCache.cpp)
  target_link_libraries(GodEyeTileCacheTests PRIVATE GodEyeDesktopCore Qt6::Test)
  add_test(NAME desktop-tile-cache COMMAND GodEyeTileCacheTests)
  set_tests_properties(desktop-tile-cache PROPERTIES TIMEOUT 30)
  qt_add_executable(GodEyeMapResourceChecks Tests/MapResourceChecks.cpp Graphics/MapWidget.cpp Graphics/TileCache.cpp)
  target_link_libraries(GodEyeMapResourceChecks PRIVATE GodEyeDesktopCore GodEyeScene Qt6::GuiPrivate Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
  target_compile_definitions(GodEyeMapResourceChecks PRIVATE GODEYE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
  qt_add_shaders(GodEyeMapResourceChecks "godeye_resource_shaders" PREFIX "/godeye"
    FILES shaders/globe.vert shaders/globe.frag)
  # Opt-in hardware timing evidence; deliberately outside CTest timing gates.
  qt_add_executable(GodEyeMapNavigationChecks Tests/MapNavigationChecks.cpp Graphics/MapWidget.cpp Graphics/TileCache.cpp)
  target_link_libraries(GodEyeMapNavigationChecks PRIVATE GodEyeDesktopCore GodEyeScene Qt6::GuiPrivate Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
  target_compile_definitions(GodEyeMapNavigationChecks PRIVATE GODEYE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
  set_target_properties(GodEyeMapNavigationChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  qt_add_shaders(GodEyeMapNavigationChecks "godeye_navigation_shaders" PREFIX "/godeye"
    FILES shaders/globe.vert shaders/globe.frag)
  set(GODEYE_NAVIGATION_BASELINE_ROOT "" CACHE PATH "Optional snapshot root containing Graphics/MapWidget and TileCache for navigation comparison")
  if(GODEYE_NAVIGATION_BASELINE_ROOT)
    foreach(godeye_baseline_file MapWidget.cpp MapWidget.h TileCache.cpp TileCache.h)
      if(NOT EXISTS "${GODEYE_NAVIGATION_BASELINE_ROOT}/Graphics/${godeye_baseline_file}")
        message(FATAL_ERROR "Missing frozen navigation baseline source: ${GODEYE_NAVIGATION_BASELINE_ROOT}/Graphics/${godeye_baseline_file}")
      endif()
    endforeach()
    qt_add_executable(GodEyeMapNavigationBaseline Tests/MapNavigationChecks.cpp
      "${GODEYE_NAVIGATION_BASELINE_ROOT}/Graphics/MapWidget.cpp"
      "${GODEYE_NAVIGATION_BASELINE_ROOT}/Graphics/MapWidget.h"
      "${GODEYE_NAVIGATION_BASELINE_ROOT}/Graphics/TileCache.cpp"
      "${GODEYE_NAVIGATION_BASELINE_ROOT}/Graphics/TileCache.h")
    # Both benchmark and generated MOC must see the frozen class declarations.
    target_include_directories(GodEyeMapNavigationBaseline BEFORE PRIVATE "${GODEYE_NAVIGATION_BASELINE_ROOT}")
    target_link_libraries(GodEyeMapNavigationBaseline PRIVATE GodEyeDesktopCore GodEyeScene Qt6::GuiPrivate Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
    target_compile_definitions(GodEyeMapNavigationBaseline PRIVATE GODEYE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
      GODEYE_NAVIGATION_GRAPHICS_ROOT="${GODEYE_NAVIGATION_BASELINE_ROOT}/Graphics")
    set_target_properties(GodEyeMapNavigationBaseline PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    qt_add_shaders(GodEyeMapNavigationBaseline "godeye_navigation_baseline_shaders" PREFIX "/godeye"
      FILES shaders/globe.vert shaders/globe.frag)
  endif()

endif()

install(TARGETS GodEyeDesktop BUNDLE DESTINATION . COMPONENT GodEye RUNTIME DESTINATION bin COMPONENT GodEye)
set(godeye_workers_source "${PROJECT_SOURCE_DIR}/native/macos/Sources/GodEyeMac/Resources")
set(godeye_worker_files SAR/sentinel1.py SAR/change_review.py SAR/registration.py SAR/requirements-monitoring.txt SAR/requirements.txt
  Optical/sentinel2.py Optical/optical_sources.py)
foreach(godeye_worker IN LISTS godeye_worker_files)
  configure_file("${godeye_workers_source}/${godeye_worker}"
    "${CMAKE_BINARY_DIR}/share/godeye/workers/${godeye_worker}" COPYONLY)
endforeach()
foreach(godeye_inference_file infer.py requirements.txt tinyyolov2-voc.contract.json)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Inference/${godeye_inference_file}"
    "${CMAKE_BINARY_DIR}/share/godeye/workers/Inference/${godeye_inference_file}" COPYONLY)
endforeach()
set(godeye_domain_resources "${PROJECT_SOURCE_DIR}/native/macos/Sources/GodEyeCore/Resources")
foreach(godeye_domain_file transit-feeds.json SGP4-LICENSE.txt)
  configure_file("${godeye_domain_resources}/${godeye_domain_file}"
    "${CMAKE_BINARY_DIR}/share/godeye/sources/${godeye_domain_file}" COPYONLY)
endforeach()
foreach(godeye_domain_directory datacenters dams telegeography_submarine_cables)
  file(COPY "${godeye_domain_resources}/${godeye_domain_directory}"
    DESTINATION "${CMAKE_BINARY_DIR}/share/godeye/sources")
endforeach()
if(APPLE)
  # Plain test executables use the same ../Resources contract as bundle binaries.
  file(COPY "${CMAKE_BINARY_DIR}/share/godeye/workers" "${CMAKE_BINARY_DIR}/share/godeye/sources"
    DESTINATION "${CMAKE_BINARY_DIR}/Resources" PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
  add_custom_command(TARGET GodEyeDesktop POST_BUILD COMMAND ${CMAKE_COMMAND}
    "-DGODEYE_WORKER_STAGE=${CMAKE_BINARY_DIR}/share/godeye"
    "-DGODEYE_RESOURCE_ROOT=$<TARGET_BUNDLE_DIR:GodEyeDesktop>/Contents/Resources"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/StageWorkers.cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/share/godeye/sources" "$<TARGET_BUNDLE_DIR:GodEyeDesktop>/Contents/Resources/sources"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:GodEyeWorkerHost>" "$<TARGET_BUNDLE_DIR:GodEyeDesktop>/Contents/MacOS/GodEyeWorkerHost")
  install(DIRECTORY "${CMAKE_BINARY_DIR}/share/godeye/workers" "${CMAKE_BINARY_DIR}/share/godeye/sources" DESTINATION GodEyeDesktop.app/Contents/Resources COMPONENT GodEye
    PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
else()
  install(TARGETS GodEyeWorkerHost RUNTIME DESTINATION bin COMPONENT GodEye)
  install(DIRECTORY "${CMAKE_BINARY_DIR}/share/godeye/workers" "${CMAKE_BINARY_DIR}/share/godeye/sources" DESTINATION share/godeye COMPONENT GodEye
    PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
  install(FILES "${CMAKE_BINARY_DIR}/SCENE-NOTICES.txt" "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION share/godeye/licenses COMPONENT GodEye)
  install(FILES "${PROJECT_SOURCE_DIR}/NOTICE.md" DESTINATION share/godeye/licenses
    RENAME PROJECT-NOTICE.md COMPONENT GodEye)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.md" DESTINATION share/godeye/licenses
    RENAME DESKTOP-NOTICE.md COMPONENT GodEye)
endif()
