cmake_minimum_required(VERSION 3.21)
project(AOWISMobile LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# AOWIS mobile application, currently retaining an instrumented renderer scene.
# Keep the proven Qt Quick/bgfx integration while product features are added.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Positioning Sensors)
qt_standard_project_setup(REQUIRES 6.5)

option(AOWIS_MOBILE_WITH_BGFX "Build the experimental bgfx/Qt Quick direct-underlay path" ON)

# Keep application artwork shared across build targets; platform-specific
# launcher/file icons are derived from assets/icons/aowis-source.png.
set(AOWIS_WINDOWS_ICON_RESOURCE "")
if(WIN32)
    set(AOWIS_WINDOWS_ICON_RESOURCE packaging/windows/aowis.rc)
endif()
qt_add_executable(aowis-mobile
    ${AOWIS_WINDOWS_ICON_RESOURCE}
    src/main.cpp
    src/map_server_settings.cpp
    src/map_server_settings.h
    src/appearance_settings.cpp
    src/appearance_settings.h
    src/location_snapshot.cpp
    src/location_snapshot.h
    src/location_service.cpp
    src/location_service.h
    src/compass_service.cpp
    src/compass_service.h
    src/compass_heading.h
    src/compass_heading_filter.h
    src/gradienter_service.cpp
    src/gradienter_service.h
    src/gradienter_math.h
    src/plumb_bob_service.cpp
    src/plumb_bob_service.h
    src/plumb_bob_math.h
    src/render_lab.cpp
    src/render_lab.h
    src/gpu_scene_geometry.cpp
    src/gpu_scene_geometry.h
    src/performance_samples.cpp
    src/performance_samples.h
    src/lifecycle_watchdog.cpp
    src/lifecycle_watchdog.h
)

set_source_files_properties(assets/icons/aowis.png PROPERTIES QT_RESOURCE_ALIAS icon.png)
qt_add_resources(aowis-mobile "aowis_application_icon"
    PREFIX "/aowis"
    FILES assets/icons/aowis.png
)

if(APPLE AND NOT ANDROID)
    set_source_files_properties(assets/icons/aowis.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(aowis-mobile PRIVATE assets/icons/aowis.icns)
    set_target_properties(aowis-mobile PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE aowis.icns)
endif()

set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(qml/UiIcon.qml PROPERTIES QT_RESOURCE_ALIAS UiIcon.qml)
set_source_files_properties(qml/CompassRose.qml PROPERTIES QT_RESOURCE_ALIAS CompassRose.qml)
set_source_files_properties(qml/GradienterVial.qml PROPERTIES QT_RESOURCE_ALIAS GradienterVial.qml)
set_source_files_properties(qml/PlumbBobInstrument.qml PROPERTIES QT_RESOURCE_ALIAS PlumbBobInstrument.qml)
qt_add_qml_module(aowis-mobile
    URI AOWIS.Mobile
    VERSION 1.0
    QML_FILES qml/Main.qml qml/UiIcon.qml qml/CompassRose.qml qml/GradienterVial.qml qml/PlumbBobInstrument.qml
)

# Qt generates the Android Manifest and deployment metadata. Keep its stock
# Activity/template so orientation, Qt library loading and lifecycle are not
# accidentally broken by a hand-written manifest for this temporary lab.
if(ANDROID)
    set_target_properties(aowis-mobile PROPERTIES
        QT_ANDROID_PACKAGE_NAME "org.aowis.mobile"
        QT_ANDROID_APP_NAME "AOWIS"
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        QT_ANDROID_APP_ICON "@mipmap/ic_launcher"
        QT_ANDROID_VERSION_CODE 1
        QT_ANDROID_VERSION_NAME "0.1"
    )
    # Qt 6.12 generates its stock Android manifest. Declare both foreground
    # permission levels here; Android lets the user grant coarse location only.
    # Never request background-location permission for this feature.
    qt_add_android_permission(aowis-mobile NAME android.permission.ACCESS_COARSE_LOCATION)
    qt_add_android_permission(aowis-mobile NAME android.permission.ACCESS_FINE_LOCATION)
endif()

target_link_libraries(aowis-mobile PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Positioning Qt6::Sensors
)

if(AOWIS_MOBILE_WITH_BGFX)
    # No FetchContent: builds must be reproducible and never download dependencies.
    # tools/init_bgfx.sh establishes a pinned git submodule (and its nested deps).
    set(AOWIS_BGFX_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/bgfx.cmake")
    if(NOT EXISTS "${AOWIS_BGFX_CMAKE_DIR}/CMakeLists.txt" OR
       NOT EXISTS "${AOWIS_BGFX_CMAKE_DIR}/bgfx/include/bgfx/bgfx.h" OR
       NOT EXISTS "${AOWIS_BGFX_CMAKE_DIR}/bx/include/bx/bx.h" OR
       NOT EXISTS "${AOWIS_BGFX_CMAKE_DIR}/bimg/include/bimg/bimg.h")
        message(FATAL_ERROR
            "bgfx.cmake and its nested submodules are not initialized.\n"
            "Run ./git_submodule_init.sh --bgfx and reconfigure, or configure with "
            "-DAOWIS_MOBILE_WITH_BGFX=OFF for the QML-only baseline.")
    endif()

    # The Qt Quick scenegraph owns the render thread, therefore bgfx must not
    # launch a second render thread for this experiment.
    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
    set(BGFX_AMALGAMATED ON CACHE BOOL "" FORCE)
    set(BX_AMALGAMATED ON CACHE BOOL "" FORCE)
    set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE)
    set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
    add_subdirectory("${AOWIS_BGFX_CMAKE_DIR}" "${CMAKE_BINARY_DIR}/bgfx-dependency" EXCLUDE_FROM_ALL)
    # Use the pinned bgfx sample's already compiled, matched vertex-color
    # shaders. No shaderc cross-compilation or downloaded shader pack needed.
    # QRC embeds both GL and GLES variants for Linux + Android deployment.
    set(AOWIS_BGFX_SHADER_ROOT "${AOWIS_BGFX_CMAKE_DIR}/bgfx/examples/runtime/shaders")
    set(AOWIS_BGFX_SHADER_FILES
        "${AOWIS_BGFX_SHADER_ROOT}/glsl/vs_cubes.bin"
        "${AOWIS_BGFX_SHADER_ROOT}/glsl/fs_cubes.bin"
        "${AOWIS_BGFX_SHADER_ROOT}/essl/vs_cubes.bin"
        "${AOWIS_BGFX_SHADER_ROOT}/essl/fs_cubes.bin"
    )
    foreach(AOWIS_SHADER IN LISTS AOWIS_BGFX_SHADER_FILES)
        if(NOT EXISTS "${AOWIS_SHADER}")
            message(FATAL_ERROR "Missing pinned bgfx example shader: ${AOWIS_SHADER}")
        endif()
    endforeach()
    qt_add_resources(aowis-mobile "aowis_lab_vertex_color_shaders"
        PREFIX "/aowis_lab/shaders"
        BASE "${AOWIS_BGFX_SHADER_ROOT}"
        FILES ${AOWIS_BGFX_SHADER_FILES}
    )
    # Shared GPU device lifecycle and view/target ownership. Mobile retains
    # the pinned bgfx.cmake runtime and its Qt-owned EGL underlay.
    # Use the pinned submodule by default. Android Docker may explicitly set
    # AOWIS_RENDER_SOURCE_DIR to its read-only /aowis-render mount.
    set(AOWIS_RENDER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/AOWIS-GUI-Render"
        CACHE PATH "AOWIS-GUI-Render source checkout (normally the pinned submodule)")
    if(NOT EXISTS "${AOWIS_RENDER_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "AOWIS-GUI-Render missing at ${AOWIS_RENDER_SOURCE_DIR}. "
            "Run ./git_submodule_init.sh --render or initialize the pinned Git submodule.")
    endif()
    set(AOWIS_RENDER_BUILD_MAP_CORE OFF CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_NETWORK_SCENE ON CACHE BOOL "" FORCE)
    add_subdirectory("${AOWIS_RENDER_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/aowis-render" EXCLUDE_FROM_ALL)
    target_sources(aowis-mobile PRIVATE
        src/gpu_scene_renderer.cpp src/gpu_scene_renderer.h)
    target_link_libraries(aowis-mobile PRIVATE AOWIS::RenderDevice AOWIS::RenderGeometry AOWIS::RenderNetwork)
    if(ANDROID)
        # bgfx's EGL backend calls ANativeWindow_setBuffersGeometry. This
        # function is provided by the Android NDK's libandroid, not EGL/GLES.
        target_link_libraries(aowis-mobile PRIVATE android)
    endif()
    target_compile_definitions(aowis-mobile PRIVATE AOWIS_LAB_WITH_BGFX=1)
else()
    target_compile_definitions(aowis-mobile PRIVATE AOWIS_LAB_WITH_BGFX=0)
endif()

# The experiment uses native OpenGL/EGL context access. It is deliberately
# limited to Linux and Android: support for other hosts needs its own adapter.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    if(AOWIS_MOBILE_WITH_BGFX)
        message(FATAL_ERROR "bgfx direct-underlay lab currently targets Linux and Android only")
    endif()
endif()

# Android executables are shared libraries. Unlike desktop executables,
# CMake requires a LIBRARY destination for Qt's Android module target.
install(TARGETS aowis-mobile
    BUNDLE DESTINATION .
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Linux desktop integration: the same icon is visible in Qt windows and in
# desktop launchers after installation. Android packages mipmap PNGs directly.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    install(FILES packaging/linux/org.aowis.mobile.desktop
        DESTINATION share/applications)
    install(FILES assets/icons/aowis.png
        DESTINATION share/icons/hicolor/512x512/apps
        RENAME org.aowis.mobile.png)
endif()

# Pure C++ geometry/camera test: no bgfx, Qt, or GPU context required.
include(CTest)
if(BUILD_TESTING)
    if(AOWIS_MOBILE_WITH_BGFX)
        # Compile and run against the production geometry headers from the
        # pinned Render submodule (not local Mobile copies). No GPU required.
        add_executable(aowis-mobile-shared-render-geometry-tests
            tests/shared_render_geometry_tests.cpp)
        target_link_libraries(aowis-mobile-shared-render-geometry-tests PRIVATE
            AOWIS::RenderGeometry)
        add_test(NAME aowis-mobile-shared-render-geometry
            COMMAND aowis-mobile-shared-render-geometry-tests)
        add_executable(aowis-mobile-shared-render-network-input-tests
            tests/shared_render_network_input_tests.cpp)
        target_link_libraries(aowis-mobile-shared-render-network-input-tests PRIVATE
            AOWIS::RenderNetwork)
        add_test(NAME aowis-mobile-shared-render-network-input
            COMMAND aowis-mobile-shared-render-network-input-tests)
    endif()
    add_executable(aowis-mobile-gpu-scene-tests
        tests/gpu_scene_geometry_tests.cpp
        src/gpu_scene_geometry.cpp
        src/performance_samples.cpp
    )
    add_test(NAME aowis-mobile-gpu-scene COMMAND aowis-mobile-gpu-scene-tests)
    add_executable(aowis-mobile-gpu-stress-tests
        tests/gpu_scene_stress_tests.cpp
        src/gpu_scene_geometry.cpp
        src/performance_samples.cpp
    )
    add_test(NAME aowis-mobile-gpu-stress COMMAND aowis-mobile-gpu-stress-tests)
    add_executable(aowis-mobile-compass-heading-tests
        tests/compass_heading_tests.cpp src/compass_heading.h)
    target_include_directories(aowis-mobile-compass-heading-tests PRIVATE src)
    add_test(NAME aowis-mobile-compass-heading COMMAND aowis-mobile-compass-heading-tests)
    add_executable(aowis-mobile-gradienter-math-tests
        tests/gradienter_math_tests.cpp src/gradienter_math.h)
    target_include_directories(aowis-mobile-gradienter-math-tests PRIVATE src)
    add_test(NAME aowis-mobile-gradienter-math COMMAND aowis-mobile-gradienter-math-tests)
    add_executable(aowis-mobile-plumb-bob-math-tests
        tests/plumb_bob_math_tests.cpp src/plumb_bob_math.h)
    target_include_directories(aowis-mobile-plumb-bob-math-tests PRIVATE src)
    add_test(NAME aowis-mobile-plumb-bob-math COMMAND aowis-mobile-plumb-bob-math-tests)
    add_executable(aowis-mobile-location-snapshot-tests
        tests/location_snapshot_tests.cpp src/location_snapshot.cpp src/location_snapshot.h)
    target_include_directories(aowis-mobile-location-snapshot-tests PRIVATE src)
    target_link_libraries(aowis-mobile-location-snapshot-tests PRIVATE Qt6::Core Qt6::Positioning)
    add_test(NAME aowis-mobile-location-snapshot COMMAND aowis-mobile-location-snapshot-tests)
    add_executable(aowis-mobile-map-server-settings-tests
        tests/map_server_settings_tests.cpp src/map_server_settings.cpp src/map_server_settings.h)
    target_include_directories(aowis-mobile-map-server-settings-tests PRIVATE src)
    target_link_libraries(aowis-mobile-map-server-settings-tests PRIVATE Qt6::Core)
    add_test(NAME aowis-mobile-map-server-settings COMMAND aowis-mobile-map-server-settings-tests)
    add_executable(aowis-mobile-appearance-settings-tests
        tests/appearance_settings_tests.cpp src/appearance_settings.cpp src/appearance_settings.h)
    target_include_directories(aowis-mobile-appearance-settings-tests PRIVATE src)
    target_link_libraries(aowis-mobile-appearance-settings-tests PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME aowis-mobile-appearance-settings COMMAND aowis-mobile-appearance-settings-tests)
    set_tests_properties(aowis-mobile-appearance-settings PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(aowis-mobile-lifecycle-tests
        tests/lifecycle_watchdog_tests.cpp src/lifecycle_watchdog.cpp)
    target_include_directories(aowis-mobile-lifecycle-tests PRIVATE src)
    add_test(NAME aowis-mobile-lifecycle COMMAND aowis-mobile-lifecycle-tests)
endif()
