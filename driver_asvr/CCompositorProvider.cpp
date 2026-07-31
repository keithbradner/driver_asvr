#include "stdafx.h"

#include "CCompositorProvider.h"
#include "DriverSettings.h"
#include "CDisplayRedirect.h"

// Defined here so the unique_ptr member sees a complete type
CCompositorProvider::CCompositorProvider() = default;
CCompositorProvider::~CCompositorProvider() = default;

vr::EVRInitError CCompositorProvider::Init(vr::IVRDriverContext *pDriverContext)
{
    VR_INIT_COMPOSITOR_DRIVER_CONTEXT(pDriverContext);

    const DriverSettings l_settings = LoadDriverSettings();
    m_displayRedirect.reset(new CDisplayRedirect(l_settings.fps));

    vr::IVRDriverLog *l_log = vr::VRDriverLog();
    if(l_log) l_log->Log("CCompositorProvider: initialized");
    return vr::VRInitError_None;
}

void CCompositorProvider::Cleanup()
{
    m_displayRedirect.reset();

    VR_CLEANUP_COMPOSITOR_DRIVER_CONTEXT();
}

const char* const* CCompositorProvider::GetInterfaceVersions()
{
    return vr::k_InterfaceVersions;
}

void* CCompositorProvider::GetComponent(const char *pchComponentNameAndVersion)
{
    void *l_result = nullptr;
    if(m_displayRedirect && (strcmp(pchComponentNameAndVersion, vr::IVRVirtualDisplay_Version) == 0)) l_result = static_cast<vr::IVRVirtualDisplay*>(m_displayRedirect.get());

    vr::IVRDriverLog *l_log = vr::VRDriverLog();
    if(l_log)
    {
        char l_line[192];
        snprintf(l_line, sizeof(l_line), "CCompositorProvider: GetComponent('%s') -> %s", pchComponentNameAndVersion, l_result ? "provided" : "null");
        l_log->Log(l_line);
    }
    return l_result;
}
