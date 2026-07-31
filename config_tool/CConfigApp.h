#pragma once

#include "CConfigCore.h"
#include "../shared/KeyBindings.h"

struct GLFWwindow;

class CConfigApp final
{
    CConfigCore *m_core;
    GLFWwindow *m_window;
    bool m_active;
    double m_lastRefresh;

    // UI edit state, loaded from the core on init/reload
    bool m_uiActivateMultipleDrivers;
    bool m_uiAllowDisplayLockedMode;
    bool m_uiDisableAsync;
    bool m_uiDoNotFadeToGrid;
    bool m_uiPauseCompositorOnStandby;
    int m_uiTurnOffScreensTimeout;
    bool m_uiForcedDriver;
    int m_uiResolution[2U];
    float m_uiFps;
    int m_uiKeyBindings[KB_Count];
    bool m_uiRestoreBackupOnUninstall;
    bool m_uiPoseResetPending;

    void PullVRSettingsToUI();
    void PushVRSettingsFromUI();
    void PullDriverSettingsToUI();
    void PushDriverSettingsFromUI();

    void DrawStatusSection();
    void DrawDriverSwitchSection();
    void DrawInstallSection();
    void DrawVRSettingsSection();
    void DrawDriverSettingsSection();
    void DrawKeyBindingsRows();
    void DrawFrame();
public:
    CConfigApp();
    ~CConfigApp();

    bool Init();
    bool Update();
    void Shutdown();
};
