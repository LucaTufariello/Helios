find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui GuiPrivate Widgets Network Concurrent ShaderTools Test)
qt_standard_project_setup()
add_executable(HeliosWorkerHost WorkerHost.cpp)
target_compile_features(HeliosWorkerHost PRIVATE cxx_std_20)
set_target_properties(HeliosWorkerHost PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_subdirectory(../scene "${CMAKE_BINARY_DIR}/native/scene")

add_library(HeliosDesktopCore STATIC
  Core/Geography.cpp Core/Investigations.cpp Core/Briefing.cpp Core/Missions.cpp Core/Monitoring.cpp Core/ScientificQueue.cpp Core/MonitoringRunner.cpp Core/Dossiers.cpp Core/Products.cpp Core/WorkerJob.cpp Core/OpticalCatalog.cpp Core/Credentials.cpp
  Core/HttpRequest.cpp Core/RadarCatalog.cpp Core/RadarDownload.cpp Core/JsonFile.cpp Core/Infrastructure.cpp Core/Timeline.cpp Core/Monitors.cpp Core/LibraryStorage.cpp Core/MapTools.cpp Core/Inference.cpp Core/DailyImagery.cpp
  Core/DomainFeeds.cpp Core/DomainParsers.cpp Core/DomainProto.cpp Core/DomainOrbit.cpp Core/DomainSocket.cpp Core/DomainNetwork.cpp Core/DomainHistory.cpp
  ../macos/Sources/CSGP4/HeliosSGP4.cpp ../macos/Sources/CSGP4/SGP4.cpp)
target_include_directories(HeliosDesktopCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/native/macos/Sources/CSGP4/include)
target_link_libraries(HeliosDesktopCore PUBLIC HeliosSpatial Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent)
target_compile_features(HeliosDesktopCore PUBLIC cxx_std_20)
if(APPLE)
  enable_language(OBJCXX)
  target_sources(HeliosDesktopCore PRIVATE Core/CredentialsMac.mm)
  target_compile_options(HeliosDesktopCore PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
  target_link_libraries(HeliosDesktopCore PRIVATE "-framework Security" "-framework Foundation" "-framework LocalAuthentication")
elseif(UNIX)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBSECRET QUIET libsecret-1)
    if(LIBSECRET_FOUND)
      target_compile_definitions(HeliosDesktopCore PRIVATE HELIOS_HAVE_LIBSECRET=1)
      target_include_directories(HeliosDesktopCore PRIVATE ${LIBSECRET_INCLUDE_DIRS})
    endif()
  endif()
endif()

qt_add_executable(HeliosDesktop MACOSX_BUNDLE
  main.cpp Graphics/TileCache.cpp Graphics/MapWidget.cpp UI/WorkspaceStyle.cpp UI/WorkspacePanel.cpp
  UI/InvestigationWorkbench.cpp UI/SiteMonitoringPanel.cpp UI/MonitoringInboxPanel.cpp UI/DossierPanel.cpp UI/MainWindow.cpp UI/MissionWindow.cpp UI/MissionEditor.cpp UI/SatelliteWindow.cpp UI/ProcessingWindow.cpp UI/RadarPanel.cpp UI/ReviewWindow.cpp UI/TimelinePanel.cpp UI/MonitorPanel.cpp UI/MapToolsWindow.cpp UI/InferenceWindow.cpp UI/DomainWindow.cpp UI/CameraViewer.cpp Validation.cpp)
target_link_libraries(HeliosDesktop PRIVATE HeliosDesktopCore HeliosScene Qt6::GuiPrivate
  Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
add_dependencies(HeliosDesktop HeliosWorkerHost)
set_target_properties(HeliosDesktop PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/HeliosInfo.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.helios.desktop.preview"
  MACOSX_BUNDLE_BUNDLE_NAME "Helios Desktop"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1")
qt_add_shaders(HeliosDesktop "helios_shaders"
  PREFIX "/helios"
  FILES shaders/globe.vert shaders/globe.frag)
qt_add_resources(HeliosDesktop "helios_qa" PREFIX "/helios" FILES Tests/review_fixture.py Tests/monitoring_fixture.py Tests/inference_fixture.py)

if(NOT MSVC)
  target_compile_options(HeliosDesktopCore PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(HeliosDesktop PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  qt_add_executable(HeliosInvestigationTests Tests/InvestigationTests.cpp)
  target_link_libraries(HeliosInvestigationTests PRIVATE HeliosDesktopCore Qt6::Test)
  target_compile_definitions(HeliosInvestigationTests PRIVATE HELIOS_TEST_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Tests/HeliosCoreTests/Fixtures")
  add_test(NAME desktop-investigations COMMAND HeliosInvestigationTests)
  set_tests_properties(desktop-investigations PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(HeliosInvestigationUiTests Tests/InvestigationUITests.cpp UI/InvestigationWorkbench.cpp UI/MissionEditor.cpp UI/WorkspaceStyle.cpp)
  target_link_libraries(HeliosInvestigationUiTests PRIVATE HeliosDesktopCore Qt6::Widgets Qt6::Test)
  add_test(NAME desktop-investigations-ui COMMAND HeliosInvestigationUiTests)
  set_tests_properties(desktop-investigations-ui PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  qt_add_executable(HeliosMonitoringUiTests Tests/MonitoringUITests.cpp UI/SiteMonitoringPanel.cpp UI/MonitoringInboxPanel.cpp UI/WorkspaceStyle.cpp)
  target_link_libraries(HeliosMonitoringUiTests PRIVATE HeliosDesktopCore Qt6::Widgets Qt6::Test)
  add_test(NAME desktop-monitoring-ui COMMAND HeliosMonitoringUiTests)
  set_tests_properties(desktop-monitoring-ui PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(HeliosMonitoringTests Tests/MonitoringTests.cpp)
  target_link_libraries(HeliosMonitoringTests PRIVATE HeliosDesktopCore Qt6::Test)
  target_compile_definitions(HeliosMonitoringTests PRIVATE HELIOS_TEST_PYTHON="${Python3_EXECUTABLE}" HELIOS_TEST_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/Tests/worker_fixture.py" HELIOS_TEST_HOST="$<TARGET_FILE:HeliosWorkerHost>")
  add_dependencies(HeliosMonitoringTests HeliosWorkerHost)
  add_test(NAME desktop-monitoring COMMAND HeliosMonitoringTests)
  set_tests_properties(desktop-monitoring PROPERTIES TIMEOUT 45)
  qt_add_executable(HeliosDossierTests Tests/DossierTests.cpp)
  target_link_libraries(HeliosDossierTests PRIVATE HeliosDesktopCore Qt6::Test)
  add_test(NAME desktop-dossiers COMMAND HeliosDossierTests)
  qt_add_executable(HeliosDossierUiTests Tests/DossierUITests.cpp UI/DossierPanel.cpp UI/MissionEditor.cpp UI/WorkspaceStyle.cpp)
  target_include_directories(HeliosDossierUiTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(HeliosDossierUiTests PRIVATE HeliosDesktopCore Qt6::Widgets Qt6::Concurrent Qt6::Test)
  add_test(NAME desktop-dossier-ui COMMAND HeliosDossierUiTests)
  set_tests_properties(desktop-dossier-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(HeliosWorkspaceUiTests Tests/WorkspaceUITests.cpp UI/WorkspacePanel.cpp UI/WorkspaceStyle.cpp)
  target_include_directories(HeliosWorkspaceUiTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(HeliosWorkspaceUiTests PRIVATE Qt6::Widgets Qt6::Test)
  add_test(NAME desktop-workspace-ui COMMAND HeliosWorkspaceUiTests)
  set_tests_properties(desktop-workspace-ui PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(HeliosDesktopTests Tests/CoreTests.cpp)
  target_link_libraries(HeliosDesktopTests PRIVATE HeliosDesktopCore Qt6::Test)
  target_compile_definitions(HeliosDesktopTests PRIVATE
    HELIOS_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Tests/HeliosCoreTests/Fixtures")
  add_test(NAME desktop-core COMMAND HeliosDesktopTests)
  qt_add_executable(HeliosSceneTests Tests/SceneTests.cpp)
  target_link_libraries(HeliosSceneTests PRIVATE HeliosScene Qt6::Test Qt6::Gui)
  add_test(NAME desktop-scene COMMAND HeliosSceneTests)
  set_tests_properties(desktop-scene PROPERTIES TIMEOUT 60)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  qt_add_executable(HeliosWorkerTests Tests/WorkerTests.cpp)
  target_link_libraries(HeliosWorkerTests PRIVATE HeliosDesktopCore Qt6::Test)
  target_compile_definitions(HeliosWorkerTests PRIVATE HELIOS_TEST_PYTHON="${Python3_EXECUTABLE}"
    HELIOS_TEST_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/Tests/worker_fixture.py"
    HELIOS_TEST_WORKER_HOST="$<TARGET_FILE:HeliosWorkerHost>")
  add_dependencies(HeliosWorkerTests HeliosWorkerHost)
  add_test(NAME desktop-worker COMMAND HeliosWorkerTests)
  set_tests_properties(desktop-worker PROPERTIES TIMEOUT 60)
  qt_add_executable(HeliosOpticalTests Tests/OpticalTests.cpp)
  target_link_libraries(HeliosOpticalTests PRIVATE HeliosDesktopCore Qt6::Test)
  target_compile_definitions(HeliosOpticalTests PRIVATE
    HELIOS_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Tests/HeliosCoreTests/Fixtures")
  add_test(NAME desktop-optical COMMAND HeliosOpticalTests)
  qt_add_executable(HeliosCredentialTests Tests/CredentialTests.cpp)
  target_link_libraries(HeliosCredentialTests PRIVATE HeliosDesktopCore Qt6::Test)
  add_test(NAME desktop-credentials COMMAND HeliosCredentialTests)
  qt_add_executable(HeliosRadarTests Tests/RadarTests.cpp)
  target_link_libraries(HeliosRadarTests PRIVATE HeliosDesktopCore Qt6::Test)
  add_test(NAME desktop-radar COMMAND HeliosRadarTests)
  qt_add_executable(HeliosDownloadTests Tests/DownloadTests.cpp)
  target_link_libraries(HeliosDownloadTests PRIVATE HeliosDesktopCore Qt6::Test)
  add_test(NAME desktop-download COMMAND HeliosDownloadTests)
  set_tests_properties(desktop-download PROPERTIES TIMEOUT 30)
  qt_add_executable(HeliosInfrastructureTests Tests/InfrastructureTests.cpp)
  target_link_libraries(HeliosInfrastructureTests PRIVATE HeliosDesktopCore Qt6::Test)
  target_compile_definitions(HeliosInfrastructureTests PRIVATE
    HELIOS_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Tests/HeliosCoreTests/Fixtures")
  add_test(NAME desktop-infrastructure COMMAND HeliosInfrastructureTests)
  qt_add_executable(HeliosMissionOperationsTests Tests/MissionOperationsTests.cpp)
  target_link_libraries(HeliosMissionOperationsTests PRIVATE HeliosDesktopCore Qt6::Test)
  add_test(NAME desktop-mission-operations COMMAND HeliosMissionOperationsTests)
  qt_add_executable(HeliosInferenceTests Tests/InferenceTests.cpp)
  target_link_libraries(HeliosInferenceTests PRIVATE HeliosDesktopCore Qt6::Test)
  target_compile_definitions(HeliosInferenceTests PRIVATE
    HELIOS_INFERENCE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/inference_fixture.py"
    HELIOS_INFERENCE_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/Inference/infer.py"
    HELIOS_TEST_WORKER_HOST="$<TARGET_FILE:HeliosWorkerHost>")
  add_dependencies(HeliosInferenceTests HeliosWorkerHost)
  add_test(NAME desktop-inference COMMAND HeliosInferenceTests)
  set_tests_properties(desktop-inference PROPERTIES TIMEOUT 60)
  qt_add_executable(HeliosInferenceUiTests Tests/InferenceUiTests.cpp UI/InferenceWindow.cpp UI/WorkspaceStyle.cpp)
  target_link_libraries(HeliosInferenceUiTests PRIVATE HeliosDesktopCore Qt6::Widgets Qt6::Concurrent Qt6::Test)
  target_compile_definitions(HeliosInferenceUiTests PRIVATE
    HELIOS_INFERENCE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/inference_fixture.py")
  set_target_properties(HeliosInferenceUiTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  add_dependencies(HeliosInferenceUiTests HeliosWorkerHost)
  add_test(NAME desktop-inference-ui COMMAND HeliosInferenceUiTests)
  set_tests_properties(desktop-inference-ui PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(HeliosDailyImageryTests Tests/DailyImageryTests.cpp)
  target_link_libraries(HeliosDailyImageryTests PRIVATE HeliosDesktopCore Qt6::Test)
  add_test(NAME desktop-daily-imagery COMMAND HeliosDailyImageryTests)
  set_tests_properties(desktop-daily-imagery PROPERTIES TIMEOUT 60)
  qt_add_executable(HeliosDomainTests Tests/DomainTests.cpp)
  target_link_libraries(HeliosDomainTests PRIVATE HeliosDesktopCore Qt6::Test)
  target_compile_definitions(HeliosDomainTests PRIVATE
    HELIOS_DOMAIN_SOURCE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Sources/HeliosCore/Resources"
    HELIOS_DOMAIN_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/native/macos/Tests/HeliosCoreTests/Fixtures")
  add_test(NAME desktop-domain COMMAND HeliosDomainTests)
  qt_add_executable(HeliosDomainUiTests Tests/DomainUITests.cpp UI/DomainWindow.cpp UI/CameraViewer.cpp UI/WorkspaceStyle.cpp)
  target_link_libraries(HeliosDomainUiTests PRIVATE HeliosDesktopCore Qt6::Widgets Qt6::Test)
  set_target_properties(HeliosDomainUiTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  add_test(NAME desktop-domain-ui COMMAND HeliosDomainUiTests)
  set_tests_properties(desktop-domain-ui PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(HeliosDomainLiveChecks Tests/DomainLiveChecks.cpp)
  target_link_libraries(HeliosDomainLiveChecks PRIVATE HeliosDesktopCore Qt6::Test)
  qt_add_executable(HeliosMapToolsTests Tests/MapToolsTests.cpp)
  target_link_libraries(HeliosMapToolsTests PRIVATE HeliosDesktopCore Qt6::Test)
  add_test(NAME desktop-map-tools COMMAND HeliosMapToolsTests)
  set_tests_properties(desktop-map-tools PROPERTIES TIMEOUT 100)
  qt_add_executable(HeliosTileCacheTests Tests/TileCacheTests.cpp Graphics/TileCache.cpp)
  target_link_libraries(HeliosTileCacheTests PRIVATE HeliosDesktopCore Qt6::Test)
  add_test(NAME desktop-tile-cache COMMAND HeliosTileCacheTests)
  set_tests_properties(desktop-tile-cache PROPERTIES TIMEOUT 30)
  qt_add_executable(HeliosMapResourceChecks Tests/MapResourceChecks.cpp Graphics/MapWidget.cpp Graphics/TileCache.cpp)
  target_link_libraries(HeliosMapResourceChecks PRIVATE HeliosDesktopCore HeliosScene Qt6::GuiPrivate Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
  target_compile_definitions(HeliosMapResourceChecks PRIVATE HELIOS_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
  qt_add_shaders(HeliosMapResourceChecks "helios_resource_shaders" PREFIX "/helios"
    FILES shaders/globe.vert shaders/globe.frag)
  # Opt-in hardware timing evidence; deliberately outside CTest timing gates.
  qt_add_executable(HeliosMapNavigationChecks Tests/MapNavigationChecks.cpp Graphics/MapWidget.cpp Graphics/TileCache.cpp)
  target_link_libraries(HeliosMapNavigationChecks PRIVATE HeliosDesktopCore HeliosScene Qt6::GuiPrivate Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
  target_compile_definitions(HeliosMapNavigationChecks PRIVATE HELIOS_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
  set_target_properties(HeliosMapNavigationChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  qt_add_shaders(HeliosMapNavigationChecks "helios_navigation_shaders" PREFIX "/helios"
    FILES shaders/globe.vert shaders/globe.frag)
  set(HELIOS_NAVIGATION_BASELINE_ROOT "" CACHE PATH "Optional snapshot root containing Graphics/MapWidget and TileCache for navigation comparison")
  if(HELIOS_NAVIGATION_BASELINE_ROOT)
    foreach(helios_baseline_file MapWidget.cpp MapWidget.h TileCache.cpp TileCache.h)
      if(NOT EXISTS "${HELIOS_NAVIGATION_BASELINE_ROOT}/Graphics/${helios_baseline_file}")
        message(FATAL_ERROR "Missing frozen navigation baseline source: ${HELIOS_NAVIGATION_BASELINE_ROOT}/Graphics/${helios_baseline_file}")
      endif()
    endforeach()
    qt_add_executable(HeliosMapNavigationBaseline Tests/MapNavigationChecks.cpp
      "${HELIOS_NAVIGATION_BASELINE_ROOT}/Graphics/MapWidget.cpp"
      "${HELIOS_NAVIGATION_BASELINE_ROOT}/Graphics/MapWidget.h"
      "${HELIOS_NAVIGATION_BASELINE_ROOT}/Graphics/TileCache.cpp"
      "${HELIOS_NAVIGATION_BASELINE_ROOT}/Graphics/TileCache.h")
    # Both benchmark and generated MOC must see the frozen class declarations.
    target_include_directories(HeliosMapNavigationBaseline BEFORE PRIVATE "${HELIOS_NAVIGATION_BASELINE_ROOT}")
    target_link_libraries(HeliosMapNavigationBaseline PRIVATE HeliosDesktopCore HeliosScene Qt6::GuiPrivate Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
    target_compile_definitions(HeliosMapNavigationBaseline PRIVATE HELIOS_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
      HELIOS_NAVIGATION_GRAPHICS_ROOT="${HELIOS_NAVIGATION_BASELINE_ROOT}/Graphics")
    set_target_properties(HeliosMapNavigationBaseline PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    qt_add_shaders(HeliosMapNavigationBaseline "helios_navigation_baseline_shaders" PREFIX "/helios"
      FILES shaders/globe.vert shaders/globe.frag)
  endif()

endif()

install(TARGETS HeliosDesktop BUNDLE DESTINATION . COMPONENT Helios RUNTIME DESTINATION bin COMPONENT Helios)
set(helios_workers_source "${PROJECT_SOURCE_DIR}/native/macos/Sources/HeliosMac/Resources")
set(helios_worker_files SAR/sentinel1.py SAR/change_review.py SAR/registration.py SAR/requirements-monitoring.txt SAR/requirements.txt
  Optical/sentinel2.py Optical/optical_sources.py)
foreach(helios_worker IN LISTS helios_worker_files)
  configure_file("${helios_workers_source}/${helios_worker}"
    "${CMAKE_BINARY_DIR}/share/helios/workers/${helios_worker}" COPYONLY)
endforeach()
foreach(helios_inference_file infer.py requirements.txt tinyyolov2-voc.contract.json)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Inference/${helios_inference_file}"
    "${CMAKE_BINARY_DIR}/share/helios/workers/Inference/${helios_inference_file}" COPYONLY)
endforeach()
set(helios_domain_resources "${PROJECT_SOURCE_DIR}/native/macos/Sources/HeliosCore/Resources")
foreach(helios_domain_file transit-feeds.json SGP4-LICENSE.txt)
  configure_file("${helios_domain_resources}/${helios_domain_file}"
    "${CMAKE_BINARY_DIR}/share/helios/sources/${helios_domain_file}" COPYONLY)
endforeach()
foreach(helios_domain_directory datacenters dams telegeography_submarine_cables)
  file(COPY "${helios_domain_resources}/${helios_domain_directory}"
    DESTINATION "${CMAKE_BINARY_DIR}/share/helios/sources")
endforeach()
if(APPLE)
  # Plain test executables use the same ../Resources contract as bundle binaries.
  file(COPY "${CMAKE_BINARY_DIR}/share/helios/workers" "${CMAKE_BINARY_DIR}/share/helios/sources"
    DESTINATION "${CMAKE_BINARY_DIR}/Resources" PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
  add_custom_command(TARGET HeliosDesktop POST_BUILD COMMAND ${CMAKE_COMMAND}
    "-DHELIOS_WORKER_STAGE=${CMAKE_BINARY_DIR}/share/helios"
    "-DHELIOS_RESOURCE_ROOT=$<TARGET_BUNDLE_DIR:HeliosDesktop>/Contents/Resources"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/StageWorkers.cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/share/helios/sources" "$<TARGET_BUNDLE_DIR:HeliosDesktop>/Contents/Resources/sources"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:HeliosWorkerHost>" "$<TARGET_BUNDLE_DIR:HeliosDesktop>/Contents/MacOS/HeliosWorkerHost")
  install(DIRECTORY "${CMAKE_BINARY_DIR}/share/helios/workers" "${CMAKE_BINARY_DIR}/share/helios/sources" DESTINATION HeliosDesktop.app/Contents/Resources COMPONENT Helios
    PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
else()
  install(TARGETS HeliosWorkerHost RUNTIME DESTINATION bin COMPONENT Helios)
  install(DIRECTORY "${CMAKE_BINARY_DIR}/share/helios/workers" "${CMAKE_BINARY_DIR}/share/helios/sources" DESTINATION share/helios COMPONENT Helios
    PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
  install(FILES "${CMAKE_BINARY_DIR}/SCENE-NOTICES.txt" "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION share/helios/licenses COMPONENT Helios)
  install(FILES "${PROJECT_SOURCE_DIR}/NOTICE.md" DESTINATION share/helios/licenses
    RENAME PROJECT-NOTICE.md COMPONENT Helios)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.md" DESTINATION share/helios/licenses
    RENAME DESKTOP-NOTICE.md COMPONENT Helios)
endif()
