#include "stdafx.h"

#include "CServerDriver.h"
#ifdef _WIN32
#include "CCompositorProvider.h"
#define ASVR_EXPORT __declspec(dllexport)
#else
#include <dlfcn.h>
#define ASVR_EXPORT __attribute__((visibility("default")))
#endif

// Full path of this module, captured at load; CServerDriver uses it to locate
// the monitor binary shipped next to the driver
std::string g_modulePath;

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /* lpReserved */)
{
    if(ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        char l_buffer[1024];
        const DWORD l_length = GetModuleFileNameA(hModule, l_buffer, sizeof(l_buffer));
        g_modulePath.assign(l_buffer, l_length);
    }
    return TRUE;
}
#endif

static CServerDriver g_serverDriver;
#ifdef _WIN32
static CCompositorProvider g_compositorProvider;
#endif

extern "C" ASVR_EXPORT void* HmdDriverFactory(const char *pInterfaceName, int *pReturnCode)
{
#ifndef _WIN32
    if(g_modulePath.empty())
    {
        Dl_info l_info = {};
        if(dladdr(reinterpret_cast<void*>(&HmdDriverFactory), &l_info) && l_info.dli_fname) g_modulePath.assign(l_info.dli_fname);
    }
#endif

    if(strcmp(pInterfaceName, vr::IServerTrackedDeviceProvider_Version) == 0)
    {
        return static_cast<vr::IServerTrackedDeviceProvider*>(&g_serverDriver);
    }
#ifdef _WIN32
    // Windows only: hands vrcompositor an in-process IVRVirtualDisplay. On Linux
    // frames arrive through the IVRDriverDirectModeComponent served from vrserver.
    if(strcmp(pInterfaceName, vr::IVRCompositorPluginProvider_Version) == 0)
    {
        return static_cast<vr::IVRCompositorPluginProvider*>(&g_compositorProvider);
    }
#endif

    if(pReturnCode) *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
