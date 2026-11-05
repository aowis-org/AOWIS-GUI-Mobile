cmake_minimum_required(VERSION 3.21)
project(AOWISMobile LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# This is an isolated rendering experiment, not the future AOWIS app runtime.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

option(AOWIS_MOBILE_WITH_BGFX "Build the experimental bgfx/Qt Quick direct-underlay path" ON)

qt_add_executable(aowis-mobile-render-lab
    src/main.cpp
    src/render_lab.cpp
    src/render_lab.h
)

set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
qt_add_qml_module(aowis-mobile-render-lab
    URI AOWIS.RenderLab
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_link_libraries(aowis-mobile-render-lab PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
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
            "Run ./tools/init_bgfx.sh and reconfigure, or configure with "
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
    target_link_libraries(aowis-mobile-render-lab PRIVATE bgfx)
    target_compile_definitions(aowis-mobile-render-lab PRIVATE AOWIS_LAB_WITH_BGFX=1)
else()
    target_compile_definitions(aowis-mobile-render-lab PRIVATE AOWIS_LAB_WITH_BGFX=0)
endif()

# The experiment uses native OpenGL/EGL context access. It is deliberately
# limited to Linux and Android: support for other hosts needs its own adapter.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    if(AOWIS_MOBILE_WITH_BGFX)
        message(FATAL_ERROR "bgfx direct-underlay lab currently targets Linux and Android only")
    endif()
endif()

install(TARGETS aowis-mobile-render-lab
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
