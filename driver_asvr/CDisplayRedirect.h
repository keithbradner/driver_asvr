#pragma once

#include "../shared/SharedFrameInfo.h"

class CDisplayRedirect final : public vr::ITrackedDeviceServerDriver, public vr::IVRVirtualDisplay
{
    struct OpenedBackbuffer
    {
        ID3D11Texture2D *m_texture;
        IDXGIKeyedMutex *m_mutex;
    };

    vr::TrackedDeviceIndex_t m_trackedDevice;
    std::string m_serial;
    float m_fps;

    ID3D11Device *m_device;
    ID3D11DeviceContext *m_context;
    uint64_t m_adapterLuid;
    bool m_deviceAttempted;
    bool m_deviceAdopted; // using the compositor's own device (in-process texture pointers)

    std::map<uint64_t, OpenedBackbuffer> m_backbuffers;
    ID3D11Texture2D *m_outputTexture;
    ID3D11Texture2D *m_flushTexture;
    HANDLE m_frameMapping;
    SharedFrameInfo *m_frameInfo;
    bool m_framePending;

    double m_qpcToSeconds;
    double m_lastVsync;
    uint64_t m_vsyncCounter;

    IDXGIAdapter1* PickAdapter(); // best hardware adapter (caller releases), records m_adapterLuid
    void ResolveAdapterLuid();
    bool EnsureDevice();
    void EnsureFrameMapping();
    void AdoptDevice(ID3D11Device *f_device);
    bool EnsureOutputTexture(const D3D11_TEXTURE2D_DESC &f_desc);
    OpenedBackbuffer* ResolveBackbuffer(uint64_t f_handle);
    double GetSeconds() const;

    // vr::ITrackedDeviceServerDriver
    vr::EVRInitError Activate(uint32_t unObjectId);
    void Deactivate();
    void EnterStandby();
    void *GetComponent(const char *pchComponentNameAndVersion);
    void DebugRequest(const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize);
    vr::DriverPose_t GetPose();

    // vr::IVRVirtualDisplay
    void Present(const vr::PresentInfo_t *pPresentInfo, uint32_t unPresentInfoSize);
    void WaitForPresent();
    bool GetTimeSinceLastVsync(float *pfSecondsSinceLastVsync, uint64_t *pulFrameCounter);
public:
    explicit CDisplayRedirect(float f_fps);
    ~CDisplayRedirect();

    const std::string& GetSerial() const;
};
