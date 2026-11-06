# The ASVR window: live view of the virtual headset, mouse/keyboard control,
# and the driver's install/settings UI. GLFW + Dear ImGui + OpenGL 3.2 on both
# platforms. Builds standalone on Windows
# (cmake -S asvr_monitor -B build/asvr_monitor -A x64) and as part of the
# root (Linux) build.
cmake_minimum_required(VERSION 3.21)
project(asvr_monitor LANGUAGES C CXX) # C is for glfw

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    # Static runtime: the shipped exe must run without the VC++ redist
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(ASVR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(VENDOR_DIR ${ASVR_ROOT}/vendor)
# The binary lands next to the driver's dev build output, where the driver
# looks for it first (see Platform::LaunchMonitor)
if(NOT DEFINED ASVR_OUTPUT_DIR)
    if(WIN32)
        set(ASVR_OUTPUT_DIR ${ASVR_ROOT}/bin/win64)
    else()
        set(ASVR_OUTPUT_DIR ${ASVR_ROOT}/bin/linux64)
    endif()
endif()

if(NOT TARGET glfw)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE) # deps/wayland XMLs are not vendored; X11 works under XWayland
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${VENDOR_DIR}/glfw glfw EXCLUDE_FROM_ALL)
endif()

if(NOT TARGET imgui)
    add_library(imgui STATIC
        ${VENDOR_DIR}/imgui/imgui.cpp
        ${VENDOR_DIR}/imgui/imgui_draw.cpp
        ${VENDOR_DIR}/imgui/imgui_tables.cpp
        ${VENDOR_DIR}/imgui/imgui_widgets.cpp
        ${VENDOR_DIR}/imgui/backends/imgui_impl_glfw.cpp
        ${VENDOR_DIR}/imgui/backends/imgui_impl_opengl3.cpp)
    target_include_directories(imgui PUBLIC ${VENDOR_DIR}/imgui ${VENDOR_DIR}/imgui/backends)
    target_link_libraries(imgui PUBLIC glfw)
endif()

find_package(OpenGL REQUIRED)

set(ASVR_MONITOR_SOURCES
    main.cpp
    CMonitorApp.cpp
    CVRLink.cpp
    CInputController.cpp
    CFrameReader.cpp
    CSettingsPanel.cpp
    CBindingsView.cpp
    CUiState.cpp
    CConfigCore.cpp
    CPlatform.cpp
    COpenVRPaths.cpp
    CVRSettings.cpp)
if(WIN32)
    list(APPEND ASVR_MONITOR_SOURCES resources.rc) # GLFW_ICON: exe icon + window icon
endif()

add_executable(asvr_monitor ${ASVR_MONITOR_SOURCES})
target_include_directories(asvr_monitor PRIVATE
    ${VENDOR_DIR}/openvr/headers
    ${VENDOR_DIR}/glm
    ${VENDOR_DIR}/json/single_include)
target_link_libraries(asvr_monitor PRIVATE imgui glfw OpenGL::GL)
# Every configuration writes to the same folder (MSVC would otherwise append
# the configuration name)
set_target_properties(asvr_monitor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${ASVR_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${ASVR_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${ASVR_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${ASVR_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${ASVR_OUTPUT_DIR}
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN")

if(WIN32)
    target_compile_definitions(asvr_monitor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(asvr_monitor PRIVATE ${VENDOR_DIR}/openvr/lib/win64/openvr_api.lib)
    # GUI subsystem in Release (no console window), console in Debug for
    # stdout logging; main() serves both via the explicit entry point
    set_target_properties(asvr_monitor PROPERTIES WIN32_EXECUTABLE "$<CONFIG:Release>")
    if(MSVC)
        target_link_options(asvr_monitor PRIVATE "$<$<CONFIG:Release>:/ENTRY:mainCRTStartup>")
    endif()
    add_custom_command(TARGET asvr_monitor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${VENDOR_DIR}/openvr/bin/win64/openvr_api.dll ${ASVR_OUTPUT_DIR}/openvr_api.dll)
else()
    target_link_libraries(asvr_monitor PRIVATE ${VENDOR_DIR}/openvr/lib/linux64/libopenvr_api.so rt)
    add_custom_command(TARGET asvr_monitor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${VENDOR_DIR}/openvr/lib/linux64/libopenvr_api.so ${ASVR_OUTPUT_DIR}/libopenvr_api.so)
endif()
