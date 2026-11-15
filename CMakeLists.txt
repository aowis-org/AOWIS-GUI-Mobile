cmake_minimum_required(VERSION 3.21)
project(AOWISMobile LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# AOWIS mobile application with the shared AOWIS map (AOWIS-GUI-Render),
# drawn inside Qt Quick's OpenGL / OpenGL ES scene graph.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Positioning Sensors OpenGL Network)
# Opt in to Qt's modern Android deployment-path behavior (Qt 6.12).
# Avoid a QTP0002 developer warning at target finalization.
if(QT_KNOWN_POLICY_QTP0002)
    qt_policy(SET QTP0002 NEW)
endif()
qt_standard_project_setup(REQUIRES 6.5)

option(AOWIS_MOBILE_WITH_BGFX "Build the AOWIS map (bgfx in Qt Quick); OFF: QML-only baseline" ON)
option(AOWIS_MOBILE_WITH_WIREGUARD "Embed the userspace WireGuard engine (AOWIS-NETWORK)" ON)

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
    src/wireguard_vpn_settings.cpp
    src/wireguard_vpn_settings.h
    src/appearance_settings.cpp
    src/appearance_settings.h
    src/language_settings.cpp
    src/language_settings.h
    src/unit_profile_settings.cpp
    src/unit_profile_settings.h
    src/unit_display_text.h
    src/unit_presented_quantities.h
    src/location_snapshot.cpp
    src/location_snapshot.h
    src/location_service.cpp
    src/location_service.h
    src/compass_service.cpp
    src/compass_service.h
    src/compass_heading.h
    src/compass_heading_filter.h
    src/compass_fusion.h
    src/compass_sensor_source.cpp
    src/compass_sensor_source.h
    src/gradienter_service.cpp
    src/gradienter_service.h
    src/gradienter_math.h
    src/plumb_bob_service.cpp
    src/plumb_bob_service.h
    src/plumb_bob_math.h
    src/flashlight_service.cpp
    src/flashlight_service.h
    src/mobile_map_view.cpp
    src/mobile_map_view.h
    src/mobile_map_math.h
    src/look_around_engine.h
    src/look_around_field.h
    src/look_around_vision.h
    src/look_around_recorder.h
    src/look_around_camera.cpp
    src/look_around_camera.h
    src/look_around_direct.h
    src/lifecycle_watchdog.cpp
    src/lifecycle_watchdog.h
)

# Use the canonical AOWIS domain model, including its unit profiles and
# canonical UCUM registry. Never duplicate MODEL headers in Mobile.
# Both native and Docker builds consume this pinned Git submodule.
set(AOWIS_MODEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/AOWIS-SERVER-MODEL")
if(NOT EXISTS "${AOWIS_MODEL_SOURCE_DIR}/CMakeLists.txt" OR
   NOT EXISTS "${AOWIS_MODEL_SOURCE_DIR}/include/aowis/model/units/unit_profile.h")
    message(FATAL_ERROR
        "AOWIS-SERVER-MODEL submodule is missing. "
        "Run ./tools/init_model.sh once, or ./git_submodule_init.sh --model "
        "if the gitlink is already registered.")
endif()
add_subdirectory("${AOWIS_MODEL_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/aowis-model" EXCLUDE_FROM_ALL)
target_link_libraries(aowis-mobile PRIVATE AOWIS::Model)

# Configuration storage (AOWIS-CONFIG), owner root "gui-mobile": native POSIX
# files (Android: the app's private configuration directory). Its own tests
# are built only in its repository (AOWIS_CONFIG_BUILD_TESTS defaults to
# top-level only).
set(AOWIS_CONFIG_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/AOWIS-CONFIG")
if(NOT EXISTS "${AOWIS_CONFIG_SOURCE_DIR}/CMakeLists.txt" OR
   NOT EXISTS "${AOWIS_CONFIG_SOURCE_DIR}/include/aowis/config/wireguard_profiles.h")
    message(FATAL_ERROR
        "AOWIS-CONFIG submodule is missing. Run ./tools/init_config.sh once, or "
        "./git_submodule_init.sh --config if the gitlink is already registered.")
endif()
if(NOT TARGET aowis::config)
    add_subdirectory("${AOWIS_CONFIG_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/aowis-config" EXCLUDE_FROM_ALL)
endif()
target_link_libraries(aowis-mobile PRIVATE aowis::config)

# Embedded userspace WireGuard (wireguard-go + gVisor netstack behind a
# loopback SOCKS5 proxy): no OS VPN, no VpnService, no root. Desktop builds
# compile the Go engine here (needs Go); the Android build passes the engine
# prebuilt for the NDK as a shared library (AOWIS_NETWORK_WIREGUARD_ARCHIVE,
# see tools/qt-android/build_android_inside_container.sh), packaged into the
# APK next to the app.
if(AOWIS_MOBILE_WITH_WIREGUARD)
    set(AOWIS_NETWORK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/AOWIS-NETWORK")
    if(NOT EXISTS "${AOWIS_NETWORK_SOURCE_DIR}/CMakeLists.txt" OR
       NOT EXISTS "${AOWIS_NETWORK_SOURCE_DIR}/include/aowis/network/wireguard_client.h")
        message(FATAL_ERROR
            "AOWIS-NETWORK submodule is missing. Run ./tools/init_network.sh once, or "
            "./git_submodule_init.sh --network if the gitlink is already registered "
            "(or configure with -DAOWIS_MOBILE_WITH_WIREGUARD=OFF).")
    endif()
    if(ANDROID AND NOT AOWIS_NETWORK_WIREGUARD_ARCHIVE)
        message(FATAL_ERROR "Android needs the prebuilt WireGuard engine: build with "
            "./compile_android.sh, or pass -DAOWIS_NETWORK_WIREGUARD_ARCHIVE=<libaowis_wireguard.so>.")
    endif()
    set(AOWIS_NETWORK_WITH_WIREGUARD ON CACHE BOOL "" FORCE)
    set(AOWIS_NETWORK_WITH_QT ON CACHE STRING "" FORCE)
    # NETWORK's own tests (Go e2e, Qt e2e) run in its repository, not here;
    # the function scope keeps BUILD_TESTING OFF local to it.
    function(aowis_mobile_add_network)
        set(BUILD_TESTING OFF)
        add_subdirectory("${AOWIS_NETWORK_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}/aowis-network" EXCLUDE_FROM_ALL)
    endfunction()
    aowis_mobile_add_network()
    if(NOT TARGET AOWIS::NetworkQt)
        message(FATAL_ERROR "AOWIS-NETWORK's Qt adapter (AOWIS::NetworkQt) was not built.")
    endif()
    target_link_libraries(aowis-mobile PRIVATE AOWIS::Network AOWIS::NetworkQt)
    target_compile_definitions(aowis-mobile PRIVATE AOWIS_MOBILE_WITH_WIREGUARD=1)
    if(ANDROID)
        set_property(TARGET aowis-mobile APPEND PROPERTY
            QT_ANDROID_EXTRA_LIBS "${AOWIS_NETWORK_WIREGUARD_ARCHIVE}")
    endif()
else()
    target_compile_definitions(aowis-mobile PRIVATE AOWIS_MOBILE_WITH_WIREGUARD=0)
endif()

# Package the editable UTF-8 translation catalog directly. This does not
# require Qt LinguistTools in the cross-compilation toolchain.
qt_add_resources(aowis-mobile "aowis_mobile_translations"
    PREFIX "/"
    FILES i18n/translations.json
)

set_source_files_properties(assets/icons/aowis.png PROPERTIES QT_RESOURCE_ALIAS icon.png)
qt_add_resources(aowis-mobile "aowis_application_icon"
    PREFIX "/aowis"
    FILES assets/icons/aowis.png
)

# Look-around diagnostics: the 360-degree test panorama
# (MobileMapView::lookAroundPanorama, docs/map.md).
set_source_files_properties(assets/panorama/look-around-test.jpg PROPERTIES
    QT_RESOURCE_ALIAS panorama/look-around-test.jpg)
qt_add_resources(aowis-mobile "aowis_look_around_panorama"
    PREFIX "/aowis"
    FILES assets/panorama/look-around-test.jpg
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
set_source_files_properties(qml/ToolMenuButton.qml PROPERTIES QT_RESOURCE_ALIAS ToolMenuButton.qml)
set_source_files_properties(qml/UnitSettingsPage.qml PROPERTIES QT_RESOURCE_ALIAS UnitSettingsPage.qml)
set_source_files_properties(qml/WireGuardSettingsPage.qml PROPERTIES QT_RESOURCE_ALIAS WireGuardSettingsPage.qml)
qt_add_qml_module(aowis-mobile
    URI AOWIS.Mobile
    VERSION 1.0
    QML_FILES qml/Main.qml qml/UiIcon.qml qml/CompassRose.qml qml/GradienterVial.qml qml/PlumbBobInstrument.qml qml/ToolMenuButton.qml qml/UnitSettingsPage.qml qml/WireGuardSettingsPage.qml
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
    # Camera permission is requested only when the flashlight is first enabled.
    qt_add_android_permission(aowis-mobile NAME android.permission.CAMERA)
endif()

target_link_libraries(aowis-mobile PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Positioning Qt6::Sensors Qt6::OpenGL
    Qt6::Network
)
if(ANDROID)
    # NDK ASensorManager / ALooper for the compass (no Qt Sensors involved),
    # NDK Camera2 + AImageReader for Look around's camera stabilization.
    target_link_libraries(aowis-mobile PRIVATE android camera2ndk mediandk)
endif()

if(AOWIS_MOBILE_WITH_BGFX)
    # The whole map stack of AOWIS-GUI-Render: geodesy, map model, imagery
    # and terrain repositories with the aowis-server-map REST client, the
    # globe terrain and network scenes and the bgfx map view. Nothing is
    # fetched: every source is a pinned Git submodule.
    set(AOWIS_RENDER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/AOWIS-GUI-Render"
        CACHE PATH "AOWIS-GUI-Render source checkout (normally the pinned submodule)")
    if(NOT EXISTS "${AOWIS_RENDER_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "AOWIS-GUI-Render missing at ${AOWIS_RENDER_SOURCE_DIR}. "
            "Run ./git_submodule_init.sh --render or initialize the pinned Git submodule.")
    endif()
    # bgfx, bx and bimg are Render's own pinned submodules (the GUI builds the
    # same revisions), so Render's committed shader binaries match. bgfx runs
    # single-threaded inside Qt Quick's render pass (Render's bgfx build script
    # sets BGFX_CONFIG_MULTITHREADED=0).
    foreach(aowis_vendor_header IN ITEMS
            external/bgfx/include/bgfx/bgfx.h external/bx/include/bx/bx.h
            external/bimg/include/bimg/bimg.h)
        if(NOT EXISTS "${AOWIS_RENDER_SOURCE_DIR}/${aowis_vendor_header}")
            message(FATAL_ERROR
                "Render's bgfx sources are not initialized (${aowis_vendor_header}).\n"
                "Run ./git_submodule_init.sh --render and reconfigure, or configure with "
                "-DAOWIS_MOBILE_WITH_BGFX=OFF for the QML-only baseline.")
        endif()
    endforeach()
    # GeographicLib: Mobile's pinned submodule, built by Render in its own
    # scope (no Mobile cache switches change). MODEL is Mobile's (added above).
    set(AOWIS_MOBILE_GEOGRAPHICLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/GeographicLib")
    if(NOT EXISTS "${AOWIS_MOBILE_GEOGRAPHICLIB_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "GeographicLib missing at ${AOWIS_MOBILE_GEOGRAPHICLIB_DIR}. "
            "Run ./git_submodule_init.sh --geographiclib.")
    endif()
    set(AOWIS_RENDER_GEOGRAPHICLIB_SOURCE_DIR "${AOWIS_MOBILE_GEOGRAPHICLIB_DIR}" CACHE PATH "" FORCE)
    # Qt Quick's OpenGL scene graph: desktop OpenGL on Linux, OpenGL ES on
    # Android. Only those shader families are embedded.
    if(ANDROID)
        set(AOWIS_BGFX_RENDERERS "OPENGLES" CACHE STRING "" FORCE)
    else()
        set(AOWIS_BGFX_RENDERERS "OPENGL" CACHE STRING "" FORCE)
    endif()
    set(AOWIS_RENDER_BUILD_DEVICE ON CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_MAP_CORE ON CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_NETWORK_SCENE ON CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_GLOBE_MODELS ON CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_GEO ON CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_TERRAIN_DATA ON CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_VIEW ON CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_GLOBE_TERRAIN ON CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_MAP_DATA ON CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_MAP_REST ON CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_GLOBE_NETWORK ON CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_QT_SURFACE ON CACHE BOOL "" FORCE)
    set(AOWIS_RENDER_BUILD_MAP ON CACHE BOOL "" FORCE)
    # Render's CPU regression tests build with Render's own entry points.
    set(AOWIS_RENDER_BUILD_GEOMETRY_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${AOWIS_RENDER_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/aowis-render" EXCLUDE_FROM_ALL)
    target_link_libraries(aowis-mobile PRIVATE
        AOWIS::RenderMap AOWIS::RenderMapRest AOWIS::RenderGeometry AOWIS::RenderNetwork)
    if(ANDROID)
        # bgfx's EGL backend calls ANativeWindow_setBuffersGeometry. This
        # function is provided by the Android NDK's libandroid, not EGL/GLES.
        target_link_libraries(aowis-mobile PRIVATE android)
    endif()
    target_compile_definitions(aowis-mobile PRIVATE AOWIS_MOBILE_WITH_MAP=1)
else()
    target_compile_definitions(aowis-mobile PRIVATE AOWIS_MOBILE_WITH_MAP=0)
endif()

# The map borrows Qt Quick's EGL context. It is limited to Linux and
# Android: other hosts need their own context adapter.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    if(AOWIS_MOBILE_WITH_BGFX)
        message(FATAL_ERROR "The Qt Quick map currently targets Linux and Android only")
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

include(CTest)
if(BUILD_TESTING)
    # Desktop safety regression: a missing hardware torch must never report ON.
    add_executable(aowis-mobile-flashlight-tests
        tests/flashlight_service_tests.cpp src/flashlight_service.cpp src/flashlight_service.h)
    target_include_directories(aowis-mobile-flashlight-tests PRIVATE src)
    target_link_libraries(aowis-mobile-flashlight-tests PRIVATE Qt6::Core)
    add_test(NAME aowis-mobile-flashlight COMMAND aowis-mobile-flashlight-tests)
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
    # Scale bar and gesture geometry of the map item: no GPU, no Render.
    add_executable(aowis-mobile-map-math-tests
        tests/mobile_map_math_tests.cpp src/mobile_map_math.h)
    target_include_directories(aowis-mobile-map-math-tests PRIVATE src)
    target_link_libraries(aowis-mobile-map-math-tests PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME aowis-mobile-map-math COMMAND aowis-mobile-map-math-tests)
    add_executable(aowis-mobile-compass-heading-tests
        tests/compass_heading_tests.cpp src/compass_heading.h src/compass_heading_filter.h src/compass_fusion.h)
    target_include_directories(aowis-mobile-compass-heading-tests PRIVATE src)
    add_test(NAME aowis-mobile-compass-heading COMMAND aowis-mobile-compass-heading-tests)
    # Look-around motion engine: pure C++, no Qt.
    add_executable(aowis-mobile-look-around-engine-tests
        tests/look_around_engine_tests.cpp src/look_around_engine.h
        src/compass_fusion.h src/compass_heading.h)
    target_include_directories(aowis-mobile-look-around-engine-tests PRIVATE src)
    add_test(NAME aowis-mobile-look-around-engine COMMAND aowis-mobile-look-around-engine-tests)
    # Look around without a gyroscope: own accelerometer + magnetometer
    # orientation (pure C++, no Qt).
    add_executable(aowis-mobile-look-around-field-tests
        tests/look_around_field_tests.cpp src/look_around_field.h src/look_around_engine.h
        src/compass_fusion.h src/compass_heading.h)
    target_include_directories(aowis-mobile-look-around-field-tests PRIVATE src)
    add_test(NAME aowis-mobile-look-around-field COMMAND aowis-mobile-look-around-field-tests)
    # Camera rotation tracking and fusion on synthetic camera images (pure C++).
    add_executable(aowis-mobile-look-around-vision-tests
        tests/look_around_vision_tests.cpp src/look_around_vision.h src/look_around_field.h
        src/look_around_engine.h src/compass_fusion.h src/compass_heading.h)
    target_include_directories(aowis-mobile-look-around-vision-tests PRIVATE src)
    add_test(NAME aowis-mobile-look-around-vision COMMAND aowis-mobile-look-around-vision-tests)
    # Direct look-around engine (the one the map uses): pure C++, no Qt.
    add_executable(aowis-mobile-look-around-direct-tests
        tests/look_around_direct_tests.cpp src/look_around_direct.h
        src/compass_fusion.h src/compass_heading.h)
    target_include_directories(aowis-mobile-look-around-direct-tests PRIVATE src)
    add_test(NAME aowis-mobile-look-around-direct COMMAND aowis-mobile-look-around-direct-tests)
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
    add_executable(aowis-mobile-gps-switch-tests
        tests/gps_switch_tests.cpp src/location_service.cpp src/location_service.h
        src/location_snapshot.cpp src/location_snapshot.h)
    target_include_directories(aowis-mobile-gps-switch-tests PRIVATE src)
    target_link_libraries(aowis-mobile-gps-switch-tests PRIVATE Qt6::Core Qt6::Positioning)
    add_test(NAME aowis-mobile-gps-switch COMMAND aowis-mobile-gps-switch-tests)
    add_executable(aowis-mobile-map-server-settings-tests
        tests/map_server_settings_tests.cpp src/map_server_settings.cpp src/map_server_settings.h)
    target_include_directories(aowis-mobile-map-server-settings-tests PRIVATE src)
    target_link_libraries(aowis-mobile-map-server-settings-tests PRIVATE Qt6::Core)
    add_test(NAME aowis-mobile-map-server-settings COMMAND aowis-mobile-map-server-settings-tests)
    if(AOWIS_MOBILE_WITH_WIREGUARD)
        add_executable(aowis-mobile-wireguard-settings-tests
            tests/wireguard_vpn_settings_tests.cpp src/wireguard_vpn_settings.cpp src/wireguard_vpn_settings.h)
        target_include_directories(aowis-mobile-wireguard-settings-tests PRIVATE src)
        target_link_libraries(aowis-mobile-wireguard-settings-tests PRIVATE Qt6::Core Qt6::Network AOWIS::Network aowis::config)
        target_compile_definitions(aowis-mobile-wireguard-settings-tests PRIVATE AOWIS_MOBILE_WITH_WIREGUARD=1)
        add_test(NAME aowis-mobile-wireguard-settings COMMAND aowis-mobile-wireguard-settings-tests)
    endif()
    add_executable(aowis-mobile-unit-registry-contract-tests
        tests/unit_registry_contract_tests.cpp)
    target_link_libraries(aowis-mobile-unit-registry-contract-tests PRIVATE AOWIS::Model)
    add_test(NAME aowis-mobile-unit-registry-contract COMMAND aowis-mobile-unit-registry-contract-tests)
    add_executable(aowis-mobile-unit-profile-settings-tests
        tests/unit_profile_settings_tests.cpp src/unit_profile_settings.cpp src/unit_profile_settings.h)
    target_include_directories(aowis-mobile-unit-profile-settings-tests PRIVATE src)
    target_link_libraries(aowis-mobile-unit-profile-settings-tests PRIVATE Qt6::Core AOWIS::Model)
    add_test(NAME aowis-mobile-unit-profile-settings COMMAND aowis-mobile-unit-profile-settings-tests)
    add_executable(aowis-mobile-language-settings-tests
        tests/language_settings_tests.cpp src/language_settings.cpp src/language_settings.h)
    target_include_directories(aowis-mobile-language-settings-tests PRIVATE src)
    target_link_libraries(aowis-mobile-language-settings-tests PRIVATE Qt6::Core Qt6::Gui)
    # Standalone test needs the same catalog as the Android application.
    qt_add_resources(aowis-mobile-language-settings-tests "aowis_language_test_catalog"
        PREFIX "/" FILES i18n/translations.json)
    add_test(NAME aowis-mobile-language-settings COMMAND aowis-mobile-language-settings-tests)
    set_tests_properties(aowis-mobile-language-settings PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
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
