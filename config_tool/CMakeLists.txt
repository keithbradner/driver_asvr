cmake_minimum_required(VERSION 3.21)
project(Config LANGUAGES C CXX) # C is for glfw

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    # Static runtime: the shipped Config.exe must run without the VC++ redist
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor)

# glfw/imgui may already exist when embedded in the root (Linux) build
if(NOT TARGET glfw)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${VENDOR_DIR}/glfw glfw)
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

add_executable(Config
    main.cpp
    CConfigApp.cpp
    CConfigCore.cpp
    CPlatform.cpp
    COpenVRPaths.cpp
    CVRSettings.cpp)
target_include_directories(Config PRIVATE
    ${VENDOR_DIR}/json/single_include)
target_link_libraries(Config PRIVATE imgui OpenGL::GL)
# GUI subsystem in Release, console in Debug (matches the monitor app's pattern)
set_target_properties(Config PROPERTIES WIN32_EXECUTABLE "$<CONFIG:Release>")
