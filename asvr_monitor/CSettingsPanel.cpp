#include "CSettingsPanel.h"
#include "../shared/KeyNames.h"

#include "imgui.h"
#include <sstream>

static const ImVec4 g_colorGood(0.35f, 0.85f, 0.4f, 1.f);
static const ImVec4 g_colorBad(0.95f, 0.35f, 0.35f, 1.f);
static const ImVec4 g_colorWarn(0.95f, 0.8f, 0.3f, 1.f);

// The "trackers" setting is a comma-separated list of role names, the same
// format the driver parses (trimmed, case-insensitive, "none" for no trackers)
static void ParseTrackerList(const std::string &f_list, bool *f_enabled)
{
    for(int i = 0; i < TR_Count; i++) f_enabled[i] = false;

    std::stringstream l_stream(f_list);
    std::string l_item;
    while(std::getline(l_stream, l_item, ','))
    {
        const size_t l_begin = l_item.find_first_not_of(" \t");
        if(l_begin == std::string::npos) continue;
        l_item = l_item.substr(l_begin, l_item.find_last_not_of(" \t") - l_begin + 1U);
        for(char &l_char : l_item) l_char = static_cast<char>(tolower(static_cast<unsigned char>(l_char)));
        for(int i = 0; i < TR_Count; i++)
        {
            if(l_item == g_trackerRoleDefs[i].m_name) f_enabled[i] = true;
        }
    }
}

static std::string FormatTrackerList(const bool *f_enabled)
{
    std::string l_list;
    for(int i = 0; i < TR_Count; i++)
    {
        if(!f_enabled[i]) continue;
        if(!l_list.empty()) l_list.push_back(',');
        l_list.append(g_trackerRoleDefs[i].m_name);
    }
    return l_list.empty() ? std::string("none") : l_list;
}

CSettingsPanel::CSettingsPanel(CConfigCore &f_core) : m_core(f_core)
{
    m_uiActivateMultipleDrivers = false;
    m_uiAllowDisplayLockedMode = false;
    m_uiDisableAsync = false;
    m_uiDoNotFadeToGrid = false;
    m_uiPauseCompositorOnStandby = true;
    m_uiTurnOffScreensTimeout = 30;
    m_uiForcedDriver = false;
    m_uiResolution[0U] = 1024;
    m_uiResolution[1U] = 768;
    m_uiFps = 60.f;
    for(int i = 0; i < KB_Count; i++) m_uiKeyBindings[i] = g_keyBindingDefs[i].m_defaultKey;
    for(int i = 0; i < TR_Count; i++) m_uiTrackers[i] = false;
    m_uiRestoreBackupOnUninstall = false;
    m_uiPoseResetPending = false;
    m_driverSettingsChanged = false;
    SyncFromCore();
}

void CSettingsPanel::SyncFromCore()
{
    PullVRSettingsToUI();
    PullDriverSettingsToUI();
}

bool CSettingsPanel::TakeDriverSettingsChanged()
{
    const bool l_result = m_driverSettingsChanged;
    m_driverSettingsChanged = false;
    return l_result;
}

void CSettingsPanel::PullVRSettingsToUI()
{
    CVRSettings &l_settings = m_core.GetVRSettings();
    m_uiActivateMultipleDrivers = l_settings.GetActivateMultipleDrivers();
    m_uiAllowDisplayLockedMode = l_settings.GetAllowDisplayLockedMode();
    m_uiDisableAsync = l_settings.GetDisableAsync();
    m_uiDoNotFadeToGrid = l_settings.GetDoNotFadeToGrid();
    m_uiPauseCompositorOnStandby = l_settings.GetPauseCompositorOnStandby();
    m_uiTurnOffScreensTimeout = l_settings.GetTurnOffScreensTimeout();
    m_uiForcedDriver = l_settings.GetForcedDriver();
}

void CSettingsPanel::PushVRSettingsFromUI()
{
    CVRSettings &l_settings = m_core.GetVRSettings();
    l_settings.SetActivateMultipleDrivers(m_uiActivateMultipleDrivers);
    l_settings.SetAllowDisplayLockedMode(m_uiAllowDisplayLockedMode);
    l_settings.SetDisableAsync(m_uiDisableAsync);
    l_settings.SetDoNotFadeToGrid(m_uiDoNotFadeToGrid);
    l_settings.SetPauseCompositorOnStandby(m_uiPauseCompositorOnStandby);
    l_settings.SetTurnOffScreensTimeout(m_uiTurnOffScreensTimeout);
    l_settings.SetForcedDriver(m_uiForcedDriver);
}

void CSettingsPanel::PullDriverSettingsToUI()
{
    CVRSettings &l_settings = m_core.GetVRSettings();
    m_uiResolution[0U] = l_settings.GetDriverWidth();
    m_uiResolution[1U] = l_settings.GetDriverHeight();
    m_uiFps = l_settings.GetDriverFPS();
    for(int i = 0; i < KB_Count; i++) m_uiKeyBindings[i] = l_settings.GetDriverKey(g_keyBindingDefs[i].m_settingsKey, g_keyBindingDefs[i].m_defaultKey);
    ParseTrackerList(l_settings.GetDriverTrackers(), m_uiTrackers);
    m_uiPoseResetPending = false;
}

void CSettingsPanel::PushDriverSettingsFromUI()
{
    CVRSettings &l_settings = m_core.GetVRSettings();
    if(m_uiResolution[0U] < 2) m_uiResolution[0U] = 2;
    if(m_uiResolution[1U] < 2) m_uiResolution[1U] = 2;
    if(m_uiFps < 1.f) m_uiFps = 1.f;
    l_settings.SetDriverResolution(m_uiResolution[0U], m_uiResolution[1U]);
    l_settings.SetDriverFPS(m_uiFps);
    for(int i = 0; i < KB_Count; i++) l_settings.SetDriverKey(g_keyBindingDefs[i].m_settingsKey, m_uiKeyBindings[i]);
    l_settings.SetDriverTrackers(FormatTrackerList(m_uiTrackers));
    if(m_uiPoseResetPending) l_settings.ResetDriverPose();
}

void CSettingsPanel::DrawStatusSection(bool f_vrConnected)
{
    ImGui::SeparatorText("Status");

    ImGui::TextDisabled("Driver folder:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", m_core.GetDriverPath().c_str());

    ImGui::TextDisabled("Driver files:");
    ImGui::SameLine();
    if(m_core.AreDriverFilesFound()) ImGui::TextColored(g_colorGood, "found");
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, g_colorBad);
        ImGui::TextWrapped("MISSING (asvr_monitor must sit next to the asvr folder, or inside asvr/bin/win64 - asvr/bin/linux64 on Linux)");
        ImGui::PopStyleColor();
    }

    ImGui::TextDisabled("Registered with SteamVR:");
    ImGui::SameLine();
    if(!m_core.GetOpenVRPaths().IsLoaded()) ImGui::TextColored(g_colorWarn, "unknown - %s", m_core.GetOpenVRPaths().GetError().c_str());
    else if(m_core.IsDriverRegistered()) ImGui::TextColored(g_colorGood, "YES");
    else ImGui::TextColored(g_colorBad, "NO");

    ImGui::TextDisabled("SteamVR (vrserver):");
    ImGui::SameLine();
    if(f_vrConnected) ImGui::TextColored(g_colorWarn, "running - restart it to apply driver changes");
    else if(m_core.IsVRServerRunning()) ImGui::TextColored(g_colorWarn, "starting");
    else ImGui::TextDisabled("not running");

    if(ImGui::Button("Refresh")) m_core.RefreshStatus();
}

void CSettingsPanel::DrawDriverSwitchSection()
{
    // The big switch: SteamVR's standard per-driver "enable" key; writes and
    // saves immediately, no separate Apply step
    const bool l_loaded = m_core.GetVRSettings().IsLoaded();
    const bool l_enabled = m_core.GetVRSettings().GetDriverEnabled();

    ImGui::BeginDisabled(!l_loaded);
    ImGui::PushStyleColor(ImGuiCol_Button, l_enabled ? ImVec4(0.13f, 0.45f, 0.18f, 1.f) : ImVec4(0.5f, 0.16f, 0.16f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, l_enabled ? ImVec4(0.16f, 0.55f, 0.22f, 1.f) : ImVec4(0.6f, 0.2f, 0.2f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, l_enabled ? ImVec4(0.11f, 0.38f, 0.15f, 1.f) : ImVec4(0.42f, 0.13f, 0.13f, 1.f));
    if(ImGui::Button(l_enabled ? "DRIVER ENABLED - click to disable" : "DRIVER DISABLED - click to enable", ImVec2(-1.f, 48.f)))
    {
        if(m_core.SetDriverEnabled(!l_enabled)) m_driverSettingsChanged = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
}

void CSettingsPanel::DrawInstallSection()
{
    ImGui::SeparatorText("Install");

    const bool l_canInstall = m_core.AreDriverFilesFound() && m_core.GetOpenVRPaths().IsLoaded();
    ImGui::BeginDisabled(!l_canInstall);
    if(ImGui::Button("Install", ImVec2(120.f, 0.f)))
    {
        if(m_core.Install())
        {
            SyncFromCore();
            m_driverSettingsChanged = true;
        }
    }
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Registers the asvr driver folder with SteamVR (openvrpaths.vrpath) and\n"
            "applies the steamvr.vrsettings values headless operation needs; your\n"
            "settings file is backed up first as steamvr.vrsettings.driver_asvr.bak.");
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!m_core.IsDriverRegistered());
    if(ImGui::Button("Uninstall", ImVec2(120.f, 0.f)))
    {
        if(m_core.Uninstall(m_uiRestoreBackupOnUninstall))
        {
            SyncFromCore();
            m_driverSettingsChanged = true;
        }
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!m_core.GetVRSettings().HasBackup());
    ImGui::Checkbox("Restore SteamVR settings backup on uninstall", &m_uiRestoreBackupOnUninstall);
    ImGui::EndDisabled();
}

void CSettingsPanel::DrawVRSettingsSection()
{
    ImGui::SeparatorText("SteamVR Settings");
    ImGui::TextWrapped("Install sets all of these to the recommended values; tweak here if needed.");

    ImGui::Checkbox("activateMultipleDrivers", &m_uiActivateMultipleDrivers);
    ImGui::Checkbox("allowDisplayLockedMode", &m_uiAllowDisplayLockedMode);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("An RDP session counts as a locked workstation and SteamVR refuses to\ncomposite app frames on one without this.");
    ImGui::Checkbox("disableAsync", &m_uiDisableAsync);
    ImGui::Checkbox("doNotFadeToGrid", &m_uiDoNotFadeToGrid);
    ImGui::Checkbox("pauseCompositorOnStandby", &m_uiPauseCompositorOnStandby);
    ImGui::Checkbox("forcedDriver (only load asvr)", &m_uiForcedDriver);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Sets steamvr.forcedDriver to \"asvr\" so SteamVR ignores every other HMD driver.\nOff by default and not part of Install; usually activateMultipleDrivers is all you need.\nTick it when another headset keeps claiming the HMD slot.");
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputInt("turnOffScreensTimeout (s)", &m_uiTurnOffScreensTimeout, 0);

    if(ImGui::Button("Apply##vr"))
    {
        PushVRSettingsFromUI();
        m_core.ApplyVRSettings();
    }
    ImGui::SameLine();
    if(ImGui::Button("Reload##vr"))
    {
        // Same file holds the driver section; refresh both UI blocks
        if(m_core.ReloadVRSettings())
        {
            SyncFromCore();
            m_driverSettingsChanged = true;
        }
    }
}

void CSettingsPanel::DrawDriverSettingsSection()
{
    ImGui::SeparatorText("Driver Settings");

    ImGui::SetNextItemWidth(160.f);
    ImGui::InputInt2("Resolution", m_uiResolution);
    ImGui::SetNextItemWidth(160.f);
    ImGui::InputFloat("FPS", &m_uiFps, 0.f, 0.f, "%.1f");

    const bool l_poseZero = m_core.GetVRSettings().IsDriverPoseZero() && !m_uiPoseResetPending;
    ImGui::BeginDisabled(l_poseZero);
    if(ImGui::Button("Reset Saved Pose")) m_uiPoseResetPending = true;
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Clears the headset pose the driver saves when SteamVR shuts down,\n"
            "so the next launch starts at the origin looking straight ahead.\n"
            "Greyed out while no pose is saved. To reset while SteamVR is\n"
            "running, press %s in the view.", GetKeyName(m_uiKeyBindings[KB_ResetHeadPose]));
    }
    if(m_uiPoseResetPending)
    {
        ImGui::SameLine();
        ImGui::TextColored(g_colorWarn, "pose will be zeroed on Save");
    }

    if(ImGui::CollapsingHeader("Virtual Trackers", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped("Vive-style trackers, one per checked role. All off by default.");
        DrawTrackerRows();
    }

    if(ImGui::CollapsingHeader("Key Bindings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped("Mouse look and Esc release are fixed; everything else rebinds. Save applies them to the view right away.");
        DrawKeyBindingsRows();
    }

    if(ImGui::Button("Save##drv"))
    {
        PushDriverSettingsFromUI();
        if(m_core.SaveDriverSettings())
        {
            m_uiPoseResetPending = false;
            m_driverSettingsChanged = true;
        }
    }
    ImGui::SameLine();
    if(ImGui::Button("Reload##drv"))
    {
        // Same file holds the SteamVR keys; refresh both UI blocks
        if(m_core.ReloadDriverSettings())
        {
            SyncFromCore();
            m_driverSettingsChanged = true;
        }
    }
}

void CSettingsPanel::DrawKeyBindingsRows()
{
    // One combo per row when the panel is narrow, two otherwise. The second
    // column starts past the widest first-column label so a long label can
    // never run underneath the neighbouring combo
    const float l_comboWidth = 110.f;
    const float l_spacing = ImGui::GetStyle().ItemSpacing.x;
    float l_secondColumn = 0.f;
    for(int i = 0; i < KB_Count; i += 2)
    {
        const float l_needed = l_comboWidth + l_spacing + ImGui::CalcTextSize(g_keyBindingDefs[i].m_label).x + l_spacing * 2.f;
        if(l_needed > l_secondColumn) l_secondColumn = l_needed;
    }
    const bool l_twoColumns = (ImGui::GetContentRegionAvail().x >= (l_secondColumn + l_comboWidth + l_spacing + 150.f));

    for(int i = 0; i < KB_Count; i++)
    {
        if(l_twoColumns && (i % 2 == 1)) ImGui::SameLine(l_secondColumn);
        ImGui::SetNextItemWidth(l_comboWidth);
        ImGui::PushID(i);
        if(ImGui::BeginCombo("##key", GetKeyName(m_uiKeyBindings[i])))
        {
            for(const auto &l_key : g_selectableKeys)
            {
                const bool l_selected = (l_key.m_code == m_uiKeyBindings[i]);
                if(ImGui::Selectable(l_key.m_name, l_selected)) m_uiKeyBindings[i] = l_key.m_code;
                if(l_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(g_keyBindingDefs[i].m_label);
        ImGui::PopID();
    }

    if(ImGui::Button("Reset Bindings to Defaults"))
    {
        for(int i = 0; i < KB_Count; i++) m_uiKeyBindings[i] = g_keyBindingDefs[i].m_defaultKey;
    }
}

void CSettingsPanel::DrawTrackerRows()
{
    const bool l_twoColumns = (ImGui::GetContentRegionAvail().x >= 400.f);
    for(int i = 0; i < TR_Count; i++)
    {
        if(l_twoColumns && (i % 2 == 1)) ImGui::SameLine(200.f);
        ImGui::PushID(i);
        ImGui::Checkbox(g_trackerRoleDefs[i].m_label, &m_uiTrackers[i]);
        ImGui::PopID();
        if(ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Trackers are created when SteamVR starts and follow the head at a\n"
                "role-appropriate offset until asvr_ctl moves them. SteamVR reads a\n"
                "tracker's role when the device first activates, so a newly checked\n"
                "role takes effect at the next SteamVR start.");
        }
    }

    if(ImGui::Button("Waist + Feet"))
    {
        for(int i = 0; i < TR_Count; i++) m_uiTrackers[i] = (i == TR_Waist) || (i == TR_LeftFoot) || (i == TR_RightFoot);
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("The three-point set most full-body apps expect.");
    ImGui::SameLine();
    if(ImGui::Button("All Trackers"))
    {
        for(int i = 0; i < TR_Count; i++) m_uiTrackers[i] = true;
    }
    ImGui::SameLine();
    if(ImGui::Button("No Trackers"))
    {
        for(int i = 0; i < TR_Count; i++) m_uiTrackers[i] = false;
    }
}

void CSettingsPanel::Draw(bool f_vrConnected)
{
    DrawStatusSection(f_vrConnected);
    ImGui::Spacing();
    DrawDriverSwitchSection();
    ImGui::Spacing();
    DrawInstallSection();
    ImGui::Spacing();
    DrawVRSettingsSection();
    ImGui::Spacing();
    DrawDriverSettingsSection();
}
