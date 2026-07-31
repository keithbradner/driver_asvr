#include "stdafx.h"

#include "CServerDriver.h"
#include "CCompositorProvider.h"

// Full path of this DLL, captured at load; CServerDriver uses it to locate the
// monitor exe shipped next to the driver
std::string g_modulePath;

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

static CServerDriver g_serverDriver;
static CCompositorProvider g_compositorProvider;

extern "C" __declspec(dllexport) void* HmdDriverFactory(const char *pInterfaceName, int *pReturnCode)
{
    if(strcmp(pInterfaceName, vr::IServerTrackedDeviceProvider_Version) == 0)
    {
        return static_cast<vr::IServerTrackedDeviceProvider*>(&g_serverDriver);
    }
    if(strcmp(pInterfaceName, vr::IVRCompositorPluginProvider_Version) == 0)
    {
        return static_cast<vr::IVRCompositorPluginProvider*>(&g_compositorProvider);
    }

    if(pReturnCode) *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
