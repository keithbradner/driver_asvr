# Linux build of the ASVR driver and tools; Windows builds with driver_asvr.sln
# (plus config_tool/CMakeLists.txt standalone) - see README.md.
cmake_minimum_required(VERSION 3.21)
project(asvr LANGUAGES C CXX)

if(WIN32)
    message(FATAL_ERROR "Use driver_asvr.sln on Windows; this CMake build is Linux-only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(ASVR_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/bin/linux64)
set(ASVR_VENDOR ${CMAKE_SOURCE_DIR}/vendor)

# vrserver runs inside Steam's pinned runtime; a static C++ runtime keeps the
# driver and tools independent of whatever libstdc++ that environment ships
add_link_options(-static-libstdc++ -static-libgcc)

# --- driver ---------------------------------------------------------------
add_library(driver_asvr SHARED
    driver_asvr/dllmain.cpp
    driver_asvr/CServerDriver.cpp
    driver_asvr/CVirtualHmd.cpp
    driver_asvr/CControllerDevice.cpp
    driver_asvr/DriverSettings.cpp
    driver_asvr/CControlServer.cpp
    driver_asvr/CControlServer_posix.cpp
    driver_asvr/Platform.cpp
    driver_asvr/linux/CVulkanContext.cpp
    driver_asvr/linux/CDirectModeOutput.cpp
    driver_asvr/linux/CFramePublisher.cpp
    ${ASVR_VENDOR}/volk/volk.c
)
set_target_properties(driver_asvr PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${ASVR_OUTPUT_DIR}
)
# Keep the statically-linked libstdc++ out of the .so's dynamic symbol table:
# vrserver's own (Steam runtime) libstdc++ would interpose ours otherwise,
# leaving iostreams with half-initialized locale facets (numbers silently
# fail to format/parse)
target_link_options(driver_asvr PRIVATE -Wl,--exclude-libs,ALL)
target_include_directories(driver_asvr PRIVATE
    driver_asvr
    ${ASVR_VENDOR}/openvr/headers
    ${ASVR_VENDOR}/glm
    ${ASVR_VENDOR}/vulkan/include
    ${ASVR_VENDOR}/volk
)
target_compile_definitions(driver_asvr PRIVATE VK_NO_PROTOTYPES)
target_link_libraries(driver_asvr PRIVATE dl pthread rt)

# --- asvr_monitor ---------------------------------------------------------
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE) # deps/wayland XMLs are not vendored; X11 works under XWayland
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(${ASVR_VENDOR}/glfw glfw EXCLUDE_FROM_ALL)
find_package(OpenGL REQUIRED)

add_executable(asvr_monitor
    asvr_monitor/main_linux.cpp
    asvr_monitor/CMonitorAppGlfw.cpp
)
set_target_properties(asvr_monitor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${ASVR_OUTPUT_DIR}
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)
target_include_directories(asvr_monitor PRIVATE
    ${ASVR_VENDOR}/openvr/headers
    ${ASVR_VENDOR}/glm
)
target_link_libraries(asvr_monitor PRIVATE
    glfw
    OpenGL::GL
    ${ASVR_VENDOR}/openvr/lib/linux64/libopenvr_api.so
)
add_custom_command(TARGET asvr_monitor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ASVR_VENDOR}/openvr/lib/linux64/libopenvr_api.so ${ASVR_OUTPUT_DIR}/libopenvr_api.so
)

# --- config tool ----------------------------------------------------------
add_subdirectory(config_tool)
set_target_properties(Config PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/config_tool)

# --- asvr_ctl -------------------------------------------------------------
add_executable(asvr_ctl
    asvr_ctl/main.cpp
    asvr_ctl/PipeClient_posix.cpp
    asvr_ctl/Screenshot_posix.cpp
)
set_target_properties(asvr_ctl PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ASVR_OUTPUT_DIR})
target_include_directories(asvr_ctl PRIVATE ${ASVR_VENDOR}/stb)
target_link_libraries(asvr_ctl PRIVATE rt)
