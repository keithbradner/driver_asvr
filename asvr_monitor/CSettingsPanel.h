#pragma once

#include "CConfigCore.h"
#include "../shared/KeyBindings.h"
#include "../shared/TrackerRoles.h"

// The hamburger panel: install/uninstall, the driver switch, the SteamVR
// flags, and the driver_asvr settings (resolution, fps, saved pose, trackers,
// key bindings). Edits live in the panel until Apply/Save writes them.
class CSettingsPanel final
{
    CConfigCore &m_core;

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
    bool m_uiTrackers[TR_Count];
    bool m_uiRestoreBackupOnUninstall;
    bool m_uiPoseResetPending;
    bool m_driverSettingsChanged; // the file was rewritten: reload what the window reads from it

    void PullVRSettingsToUI();
    void PushVRSettingsFromUI();
    void PullDriverSettingsToUI();
    void PushDriverSettingsFromUI();

    void DrawStatusSection(bool f_vrConnected);
    void DrawDriverSwitchSection();
    void DrawInstallSection();
    void DrawVRSettingsSection();
    void DrawDriverSettingsSection();
    void DrawKeyBindingsRows();
    void DrawTrackerRows();
public:
    explicit CSettingsPanel(CConfigCore &f_core);

    // Re-reads every edit field from the core (after an install from the start screen, say)
    void SyncFromCore();
    // Current key bindings as edited in the panel are not live; the saved ones are
    void Draw(bool f_vrConnected);
    // True once after Save/Reload/Install/Uninstall changed the settings file
    bool TakeDriverSettingsChanged();
};
