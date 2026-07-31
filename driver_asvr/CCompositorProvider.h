#pragma once

#include <memory>

class CDisplayRedirect;

// Loaded by vrcompositor.exe; hands it the IVRVirtualDisplay component so final
// frames are presented in-process (vrserver-side components are never called).
class CCompositorProvider final : public vr::IVRCompositorPluginProvider
{
public:
    CCompositorProvider();
    ~CCompositorProvider();

private:
    // vr::IVRCompositorPluginProvider
    vr::EVRInitError Init(vr::IVRDriverContext *pDriverContext);
    void Cleanup();
    const char * const *GetInterfaceVersions();
    void *GetComponent(const char *pchComponentNameAndVersion);

    std::unique_ptr<CDisplayRedirect> m_displayRedirect;
};
