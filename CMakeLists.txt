# Linux build of the ASVR driver and tools. On Windows the driver and asvr_ctl
# build with driver_asvr.sln and the monitor with asvr_monitor/CMakeLists.txt
# standalone - see README.md.
cmake_minimum_required(VERSION 3.21)
project(asvr LANGUAGES C CXX)

if(WIN32)
    message(FATAL_ERROR "Use driver_asvr.sln (driver + asvr_ctl) and 'cmake -S asvr_monitor' on Windows; this top-level CMake build is Linux-only")
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
    driver_asvr/CTrackerDevice.cpp
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

# --- asvr_monitor (window + settings UI) ------------------------------------
# Defines the asvr_monitor target; picks up ASVR_OUTPUT_DIR for its binary and
# the libopenvr_api.so copy next to it
add_subdirectory(asvr_monitor)

# --- asvr_ctl -------------------------------------------------------------
add_executable(asvr_ctl
    asvr_ctl/main.cpp
    asvr_ctl/PipeClient_posix.cpp
    asvr_ctl/Screenshot.cpp
)
set_target_properties(asvr_ctl PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ASVR_OUTPUT_DIR})
target_include_directories(asvr_ctl PRIVATE ${ASVR_VENDOR}/stb)
target_link_libraries(asvr_ctl PRIVATE rt)
