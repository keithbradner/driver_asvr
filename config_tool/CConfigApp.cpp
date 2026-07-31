#include "CConfigApp.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

const ImVec4 g_colorGood(0.35f, 0.85f, 0.4f, 1.f);
const ImVec4 g_colorBad(0.95f, 0.35f, 0.35f, 1.f);
const ImVec4 g_colorWarn(0.95f, 0.8f, 0.3f, 1.f);

// Keys offered by the binding combos, stored as Windows VK codes (the monitor
// polls them with GetAsyncKeyState)
struct SelectableKey
{
    int m_code;
    const char *m_name;
};
const SelectableKey g_selectableKeys[]
{
    { 0x01, "Left Mouse" }, { 0x02, "Right Mouse" }, { 0x04, "Middle Mouse" },
    { 0x05, "Mouse 4" }, { 0x06, "Mouse 5" },
    { 0x41, "A" }, { 0x42, "B" }, { 0x43, "C" }, { 0x44, "D" }, { 0x45, "E" }, { 0x46, "F" },
    { 0x47, "G" }, { 0x48, "H" }, { 0x49, "I" }, { 0x4A, "J" }, { 0x4B, "K" }, { 0x4C, "L" },
    { 0x4D, "M" }, { 0x4E, "N" }, { 0x4F, "O" }, { 0x50, "P" }, { 0x51, "Q" }, { 0x52, "R" },
    { 0x53, "S" }, { 0x54, "T" }, { 0x55, "U" }, { 0x56, "V" }, { 0x57, "W" }, { 0x58, "X" },
    { 0x59, "Y" }, { 0x5A, "Z" },
    { 0x30, "0" }, { 0x31, "1" }, { 0x32, "2" }, { 0x33, "3" }, { 0x34, "4" },
    { 0x35, "5" }, { 0x36, "6" }, { 0x37, "7" }, { 0x38, "8" }, { 0x39, "9" },
    { 0x20, "Space" }, { 0x09, "Tab" }, { 0x0D, "Enter" }, { 0x08, "Backspace" },
    { 0x10, "Shift" }, { 0x11, "Ctrl" }, { 0x12, "Alt" },
    { 0x26, "Up Arrow" }, { 0x28, "Down Arrow" }, { 0x25, "Left Arrow" }, { 0x27, "Right Arrow" },
    { 0x21, "Page Up" }, { 0x22, "Page Down" }, { 0x24, "Home" }, { 0x23, "End" },
    { 0x2D, "Insert" }, { 0x2E, "Delete" },
    { 0x60, "Numpad 0" }, { 0x61, "Numpad 1" }, { 0x62, "Numpad 2" }, { 0x63, "Numpad 3" },
    { 0x64, "Numpad 4" }, { 0x65, "Numpad 5" }, { 0x66, "Numpad 6" }, { 0x67, "Numpad 7" },
    { 0x68, "Numpad 8" }, { 0x69, "Numpad 9" },
    { 0x6A, "Numpad *" }, { 0x6B, "Numpad +" }, { 0x6D, "Numpad -" }, { 0x6E, "Numpad ." }, { 0x6F, "Numpad /" },
    { 0x70, "F1" }, { 0x71, "F2" }, { 0x72, "F3" }, { 0x73, "F4" }, { 0x74, "F5" }, { 0x75, "F6" },
    { 0x76, "F7" }, { 0x77, "F8" }, { 0x78, "F9" }, { 0x79, "F10" }, { 0x7A, "F11" }, { 0x7B, "F12" },
    { 0xBC, "Comma" }, { 0xBE, "Period" }, { 0xBF, "Slash" }, { 0xBA, "Semicolon" },
    { 0xDE, "Apostrophe" }, { 0xDB, "Left Bracket" }, { 0xDD, "Right Bracket" }, { 0xC0, "Backtick" }
};

static const char* GetKeyName(int f_code)
{
    for(const auto &l_key : g_selectableKeys)
    {
        if(l_key.m_code == f_code) return l_key.m_name;
    }
    return "(unknown)";
}

CConfigApp::CConfigApp()
{
    m_core = nullptr;
    m_window = nullptr;
    m_active = false;
    m_lastRefresh = 0.0;

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
    m_uiRestoreBackupOnUninstall = false;
    m_uiPoseResetPending = false;
}

CConfigApp::~CConfigApp()
{
}

bool CConfigApp::Init()
{
    if(m_active) return true;

    if(!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    m_window = glfwCreateWindow(560, 1000, "ASVR Config", NULL, NULL);
    if(!m_window)
    {
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // no imgui.ini clutter next to the exe
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    m_core = new CConfigCore();
    m_core->Initialize();
    PullVRSettingsToUI();
    PullDriverSettingsToUI();
    m_lastRefresh = glfwGetTime();

    m_active = true;
    return true;
}

void CConfigApp::Shutdown()
{
    if(!m_active) return;

    delete m_core;
    m_core = nullptr;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_window);
    m_window = nullptr;
    glfwTerminate();

    m_active = false;
}

void CConfigApp::PullVRSettingsToUI()
{
    CVRSettings &l_settings = m_core->GetVRSettings();
    m_uiActivateMultipleDrivers = l_settings.GetActivateMultipleDrivers();
    m_uiAllowDisplayLockedMode = l_settings.GetAllowDisplayLockedMode();
    m_uiDisableAsync = l_settings.GetDisableAsync();
    m_uiDoNotFadeToGrid = l_settings.GetDoNotFadeToGrid();
    m_uiPauseCompositorOnStandby = l_settings.GetPauseCompositorOnStandby();
    m_uiTurnOffScreensTimeout = l_settings.GetTurnOffScreensTimeout();
    m_uiForcedDriver = l_settings.GetForcedDriver();
}

void CConfigApp::PushVRSettingsFromUI()
{
    CVRSettings &l_settings = m_core->GetVRSettings();
    l_settings.SetActivateMultipleDrivers(m_uiActivateMultipleDrivers);
    l_settings.SetAllowDisplayLockedMode(m_uiAllowDisplayLockedMode);
    l_settings.SetDisableAsync(m_uiDisableAsync);
    l_settings.SetDoNotFadeToGrid(m_uiDoNotFadeToGrid);
    l_settings.SetPauseCompositorOnStandby(m_uiPauseCompositorOnStandby);
    l_settings.SetTurnOffScreensTimeout(m_uiTurnOffScreensTimeout);
    l_settings.SetForcedDriver(m_uiForcedDriver);
}

void CConfigApp::PullDriverSettingsToUI()
{
    CVRSettings &l_settings = m_core->GetVRSettings();
    m_uiResolution[0U] = l_settings.GetDriverWidth();
    m_uiResolution[1U] = l_settings.GetDriverHeight();
    m_uiFps = l_settings.GetDriverFPS();
    for(int i = 0; i < KB_Count; i++) m_uiKeyBindings[i] = l_settings.GetDriverKey(g_keyBindingDefs[i].m_settingsKey, g_keyBindingDefs[i].m_defaultKey);
    m_uiPoseResetPending = false;
}

void CConfigApp::PushDriverSettingsFromUI()
{
    CVRSettings &l_settings = m_core->GetVRSettings();
    if(m_uiResolution[0U] < 2) m_uiResolution[0U] = 2;
    if(m_uiResolution[1U] < 2) m_uiResolution[1U] = 2;
    if(m_uiFps < 1.f) m_uiFps = 1.f;
    l_settings.SetDriverResolution(m_uiResolution[0U], m_uiResolution[1U]);
    l_settings.SetDriverFPS(m_uiFps);
    for(int i = 0; i < KB_Count; i++) l_settings.SetDriverKey(g_keyBindingDefs[i].m_settingsKey, m_uiKeyBindings[i]);
    if(m_uiPoseResetPending) l_settings.ResetDriverPose();
}

void CConfigApp::DrawStatusSection()
{
    ImGui::SeparatorText("Status");

    ImGui::TextDisabled("Driver folder:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", m_core->GetDriverPath().c_str());

    ImGui::TextDisabled("Driver files:");
    ImGui::SameLine();
    if(m_core->AreDriverFilesFound()) ImGui::TextColored(g_colorGood, "found");
    else ImGui::TextColored(g_colorBad, "MISSING (Config must sit next to the asvr folder)");

    ImGui::TextDisabled("Registered with SteamVR:");
    ImGui::SameLine();
    if(!m_core->GetOpenVRPaths().IsLoaded()) ImGui::TextColored(g_colorWarn, "unknown - %s", m_core->GetOpenVRPaths().GetError().c_str());
    else if(m_core->IsDriverRegistered()) ImGui::TextColored(g_colorGood, "YES");
    else ImGui::TextColored(g_colorBad, "NO");

    ImGui::TextDisabled("SteamVR (vrserver):");
    ImGui::SameLine();
    if(m_core->IsVRServerRunning()) ImGui::TextColored(g_colorWarn, "running - restart it to apply changes");
    else ImGui::TextDisabled("not running");

    if(ImGui::Button("Refresh")) m_core->RefreshStatus();
}

void CConfigApp::DrawDriverSwitchSection()
{
    // The big switch: SteamVR's standard per-driver "enable" key; writes and
    // saves immediately, no separate Apply step
    const bool l_loaded = m_core->GetVRSettings().IsLoaded();
    const bool l_enabled = m_core->GetVRSettings().GetDriverEnabled();

    ImGui::BeginDisabled(!l_loaded);
    ImGui::PushStyleColor(ImGuiCol_Button, l_enabled ? ImVec4(0.13f, 0.45f, 0.18f, 1.f) : ImVec4(0.5f, 0.16f, 0.16f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, l_enabled ? ImVec4(0.16f, 0.55f, 0.22f, 1.f) : ImVec4(0.6f, 0.2f, 0.2f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, l_enabled ? ImVec4(0.11f, 0.38f, 0.15f, 1.f) : ImVec4(0.42f, 0.13f, 0.13f, 1.f));
    if(ImGui::Button(l_enabled ? "DRIVER ENABLED - click to disable" : "DRIVER DISABLED - click to enable", ImVec2(-1.f, 48.f)))
    {
        m_core->SetDriverEnabled(!l_enabled);
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
}

void CConfigApp::DrawInstallSection()
{
    ImGui::SeparatorText("Install");

    const bool l_canInstall = m_core->AreDriverFilesFound() && m_core->GetOpenVRPaths().IsLoaded();
    ImGui::BeginDisabled(!l_canInstall);
    if(ImGui::Button("Install", ImVec2(120.f, 0.f)))
    {
        if(m_core->Install())
        {
            PullVRSettingsToUI();
            PullDriverSettingsToUI();
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!m_core->IsDriverRegistered());
    if(ImGui::Button("Uninstall", ImVec2(120.f, 0.f)))
    {
        if(m_core->Uninstall(m_uiRestoreBackupOnUninstall))
        {
            PullVRSettingsToUI();
            PullDriverSettingsToUI();
        }
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!m_core->GetVRSettings().HasBackup());
    ImGui::Checkbox("Restore SteamVR settings backup on uninstall", &m_uiRestoreBackupOnUninstall);
    ImGui::EndDisabled();
}

void CConfigApp::DrawVRSettingsSection()
{
    ImGui::SeparatorText("SteamVR Settings");
    ImGui::TextDisabled("Install sets all of these to the recommended values; tweak here if needed.");

    ImGui::Checkbox("activateMultipleDrivers", &m_uiActivateMultipleDrivers);
    ImGui::SameLine(280.f);
    ImGui::Checkbox("allowDisplayLockedMode", &m_uiAllowDisplayLockedMode);
    ImGui::Checkbox("disableAsync", &m_uiDisableAsync);
    ImGui::SameLine(280.f);
    ImGui::Checkbox("doNotFadeToGrid", &m_uiDoNotFadeToGrid);
    ImGui::Checkbox("pauseCompositorOnStandby", &m_uiPauseCompositorOnStandby);
    ImGui::SameLine(280.f);
    ImGui::Checkbox("forcedDriver (only load asvr)", &m_uiForcedDriver);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Sets steamvr.forcedDriver to \"asvr\" so SteamVR ignores every other HMD driver.\nOff by default and not part of Install; usually activateMultipleDrivers is all you need.");
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputInt("turnOffScreensTimeout (s)", &m_uiTurnOffScreensTimeout, 0);

    if(ImGui::Button("Apply##vr"))
    {
        PushVRSettingsFromUI();
        m_core->ApplyVRSettings();
    }
    ImGui::SameLine();
    if(ImGui::Button("Reload##vr"))
    {
        // Same file holds the driver section; refresh both UI blocks
        if(m_core->ReloadVRSettings())
        {
            PullVRSettingsToUI();
            PullDriverSettingsToUI();
        }
    }
}

void CConfigApp::DrawDriverSettingsSection()
{
    ImGui::SeparatorText("Driver Settings (driver_asvr section)");

    ImGui::SetNextItemWidth(160.f);
    ImGui::InputInt2("Resolution", m_uiResolution);
    ImGui::SetNextItemWidth(160.f);
    ImGui::InputFloat("FPS", &m_uiFps, 0.f, 0.f, "%.1f");

    const bool l_poseZero = m_core->GetVRSettings().IsDriverPoseZero() && !m_uiPoseResetPending;
    ImGui::BeginDisabled(l_poseZero);
    if(ImGui::Button("Reset Saved Pose")) m_uiPoseResetPending = true;
    ImGui::EndDisabled();
    if(m_uiPoseResetPending)
    {
        ImGui::SameLine();
        ImGui::TextColored(g_colorWarn, "pose will be zeroed on Save");
    }

    if(ImGui::CollapsingHeader("Monitor Key Bindings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("Mouse look and Esc release are fixed; everything else rebinds.");
        DrawKeyBindingsRows();
    }

    if(ImGui::Button("Save##drv"))
    {
        PushDriverSettingsFromUI();
        if(m_core->SaveDriverSettings()) m_uiPoseResetPending = false;
    }
    ImGui::SameLine();
    if(ImGui::Button("Reload##drv"))
    {
        // Same file holds the SteamVR keys; refresh both UI blocks
        if(m_core->ReloadDriverSettings())
        {
            PullDriverSettingsToUI();
            PullVRSettingsToUI();
        }
    }
}

void CConfigApp::DrawKeyBindingsRows()
{
    // Two combos per row; saved together with the rest of the driver settings
    for(int i = 0; i < KB_Count; i++)
    {
        if(i % 2 == 1) ImGui::SameLine(280.f);
        ImGui::SetNextItemWidth(110.f);
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

void CConfigApp::DrawFrame()
{
    const ImGuiViewport *l_viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(l_viewport->WorkPos);
    ImGui::SetNextWindowSize(l_viewport->WorkSize);
    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    DrawStatusSection();
    ImGui::Spacing();
    DrawDriverSwitchSection();
    ImGui::Spacing();
    DrawInstallSection();
    ImGui::Spacing();
    DrawVRSettingsSection();
    ImGui::Spacing();
    DrawDriverSettingsSection();

    // Status line: pinned to the bottom while everything fits, inline once the
    // content scrolls (pinning would land mid-content in scroll space)
    const std::string &l_message = m_core->GetLastMessage();
    if(!l_message.empty())
    {
        if(ImGui::GetScrollMaxY() == 0.f) ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing());
        ImGui::Separator();
        ImGui::TextColored(m_core->IsLastMessageError() ? g_colorBad : g_colorGood, "%s", l_message.c_str());
    }

    ImGui::End();
}

bool CConfigApp::Update()
{
    if(!m_active || glfwWindowShouldClose(m_window)) return false;

    glfwWaitEventsTimeout(0.1);

    const double l_now = glfwGetTime();
    if((l_now - m_lastRefresh) >= 2.0)
    {
        m_core->RefreshStatus();
        m_lastRefresh = l_now;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    DrawFrame();
    ImGui::Render();

    int l_width = 0, l_height = 0;
    glfwGetFramebufferSize(m_window, &l_width, &l_height);
    glViewport(0, 0, l_width, l_height);
    glClearColor(0.08f, 0.08f, 0.1f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_window);

    return true;
}
