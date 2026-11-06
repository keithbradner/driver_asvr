#include "CBindingsView.h"
#include "CInputController.h"
#include "../shared/KeyBindings.h"
#include "../shared/KeyNames.h"

#include "imgui.h"
#include "GLFW/glfw3.h" // key constants for the caps that share a VK code
#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string>
#include <vector>

namespace
{

enum ECategory
{
    C_HeadMove = 0,
    C_HeadRotate,
    C_HeadReset,
    C_Controller,
    C_Buttons,
    C_Joystick,
    C_Modes,
    C_Fixed, // not rebindable: Esc releases the mouse, mouse motion looks or aims
    C_Count
};

const ImVec4 g_categoryColors[C_Count]
{
    ImVec4(0.20f, 0.45f, 0.80f, 1.f),
    ImVec4(0.15f, 0.58f, 0.60f, 1.f),
    ImVec4(0.32f, 0.42f, 0.62f, 1.f),
    ImVec4(0.85f, 0.50f, 0.15f, 1.f),
    ImVec4(0.25f, 0.62f, 0.30f, 1.f),
    ImVec4(0.60f, 0.36f, 0.78f, 1.f),
    ImVec4(0.75f, 0.62f, 0.18f, 1.f),
    ImVec4(0.42f, 0.43f, 0.48f, 1.f),
};
const char *g_categoryNames[C_Count] = { "Head move", "Head rotate", "Head reset", "Controller", "Buttons", "Joystick", "Modes", "Fixed" };

ECategory CategoryOf(int f_binding)
{
    switch(f_binding)
    {
        case KB_MoveForward: case KB_MoveBackward: case KB_MoveLeft: case KB_MoveRight: case KB_MoveUp: case KB_MoveDown:
            return C_HeadMove;
        case KB_YawLeft: case KB_YawRight: case KB_PitchUp: case KB_PitchDown: case KB_RollLeft: case KB_RollRight:
            return C_HeadRotate;
        case KB_ResetHeadPose:
            return C_HeadReset;
        case KB_SystemButton: case KB_Trigger: case KB_Grip: case KB_ButtonA: case KB_ButtonB:
            return C_Buttons;
        case KB_SwitchController: case KB_MouseAim:
            return C_Modes;
        case KB_ControllerForward: case KB_ControllerBackward: case KB_ControllerLeft: case KB_ControllerRight: case KB_ResetControllers:
            return C_Controller;
        default:
            return C_Joystick;
    }
}

// What fits on a key cap, in the order of EKeyBinding
const char *g_shortLabels[KB_Count]
{
    "fwd", "back", "left", "right", "up", "down",
    "yaw\nleft", "yaw\nright", "pitch\nup", "pitch\ndown", "roll\nleft", "roll\nright",
    "reset\nhead", "system",
    "trigger", "grip", "switch\nhand", "mouse\naim", "A", "B",
    "ctrl\nfwd", "ctrl\nback", "ctrl\nleft", "ctrl\nright", "reset\nctrls",
    "joy\nup", "joy\ndown", "joy\nleft", "joy\nright",
};

const char* ShortLabel(int f_binding, bool f_leftActive)
{
    if(f_leftActive && (f_binding == KB_ButtonA)) return "X";
    if(f_leftActive && (f_binding == KB_ButtonB)) return "Y";
    return g_shortLabels[f_binding];
}

// Key caps in keyboard units: a standard ANSI layout, the navigation cluster,
// the numpad and a mouse whose buttons are caps too
struct KeyCap
{
    int m_vk;             // the binding code the cap shows
    const char *m_legend;
    float m_x;
    float m_y;
    float m_w;
    float m_h;
    int m_glfwKey;        // the physical key that lights it; GLFW_KEY_UNKNOWN = whatever m_vk maps to
};

const float g_layoutWidthUnits = 27.6f;
const float g_keyboardHeightUnits = 6.25f;
const float g_legendRowUnits = 1.1f;
const float g_mouseLeft = 23.9f; // the mouse body sits right of the numpad
const float g_mouseWidth = 3.4f;
const float g_mouseTop = 0.4f;
const float g_mouseHeight = 5.f;

const char *g_letterLegends[26] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z" };
const char *g_digitLegends[10] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
const char *g_functionLegends[12] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };

std::vector<KeyCap> BuildLayout()
{
    std::vector<KeyCap> l_caps;
    // Both sides of a modifier and both Enter keys share one VK code (that is
    // what a binding holds and what GetAsyncKeyState meant), so those caps
    // name their physical key explicitly
    auto l_add = [&l_caps](int f_vk, const char *f_legend, float f_x, float f_y, float f_w = 1.f, float f_h = 1.f, int f_glfwKey = GLFW_KEY_UNKNOWN)
    {
        l_caps.push_back({ f_vk, f_legend, f_x, f_y, f_w, f_h, f_glfwKey });
    };
    auto l_letter = [&l_add](char f_letter, float f_x, float f_y)
    {
        l_add(0x41 + (f_letter - 'A'), g_letterLegends[f_letter - 'A'], f_x, f_y);
    };

    // Function row
    l_add(0x1B, "Esc", 0.f, 0.f);
    for(int i = 0; i < 4; i++) l_add(0x70 + i, g_functionLegends[i], 2.f + i, 0.f);
    for(int i = 0; i < 4; i++) l_add(0x74 + i, g_functionLegends[4 + i], 6.5f + i, 0.f);
    for(int i = 0; i < 4; i++) l_add(0x78 + i, g_functionLegends[8 + i], 11.f + i, 0.f);
    l_add(0x2C, "PrtSc", 15.5f, 0.f);
    l_add(0x91, "ScrLk", 16.5f, 0.f);
    l_add(0x13, "Pause", 17.5f, 0.f);

    // Number row
    float l_y = 1.25f;
    l_add(0xC0, "`", 0.f, l_y);
    for(int i = 0; i < 9; i++) l_add(0x31 + i, g_digitLegends[1 + i], 1.f + i, l_y);
    l_add(0x30, g_digitLegends[0], 10.f, l_y);
    l_add(0xBD, "-", 11.f, l_y);
    l_add(0xBB, "=", 12.f, l_y);
    l_add(0x08, "Bksp", 13.f, l_y, 2.f);
    l_add(0x2D, "Ins", 15.5f, l_y);
    l_add(0x24, "Home", 16.5f, l_y);
    l_add(0x21, "PgUp", 17.5f, l_y);
    l_add(0x90, "Num", 19.f, l_y);
    l_add(0x6F, "/", 20.f, l_y);
    l_add(0x6A, "*", 21.f, l_y);
    l_add(0x6D, "-", 22.f, l_y);

    // QWERTY row
    l_y = 2.25f;
    l_add(0x09, "Tab", 0.f, l_y, 1.5f);
    const char *l_rowQ = "QWERTYUIOP";
    for(int i = 0; i < 10; i++) l_letter(l_rowQ[i], 1.5f + i, l_y);
    l_add(0xDB, "[", 11.5f, l_y);
    l_add(0xDD, "]", 12.5f, l_y);
    l_add(0xDC, "\\", 13.5f, l_y, 1.5f);
    l_add(0x2E, "Del", 15.5f, l_y);
    l_add(0x23, "End", 16.5f, l_y);
    l_add(0x22, "PgDn", 17.5f, l_y);
    l_add(0x67, "7", 19.f, l_y);
    l_add(0x68, "8", 20.f, l_y);
    l_add(0x69, "9", 21.f, l_y);
    l_add(0x6B, "+", 22.f, l_y, 1.f, 2.f);

    // Home row
    l_y = 3.25f;
    l_add(0x14, "Caps", 0.f, l_y, 1.75f);
    const char *l_rowA = "ASDFGHJKL";
    for(int i = 0; i < 9; i++) l_letter(l_rowA[i], 1.75f + i, l_y);
    l_add(0xBA, ";", 10.75f, l_y);
    l_add(0xDE, "'", 11.75f, l_y);
    l_add(0x0D, "Enter", 12.75f, l_y, 2.25f, 1.f, GLFW_KEY_ENTER);
    l_add(0x64, "4", 19.f, l_y);
    l_add(0x65, "5", 20.f, l_y);
    l_add(0x66, "6", 21.f, l_y);

    // Shift row
    l_y = 4.25f;
    l_add(0x10, "Shift", 0.f, l_y, 2.25f, 1.f, GLFW_KEY_LEFT_SHIFT);
    const char *l_rowZ = "ZXCVBNM";
    for(int i = 0; i < 7; i++) l_letter(l_rowZ[i], 2.25f + i, l_y);
    l_add(0xBC, ",", 9.25f, l_y);
    l_add(0xBE, ".", 10.25f, l_y);
    l_add(0xBF, "/", 11.25f, l_y);
    l_add(0x10, "Shift", 12.25f, l_y, 2.75f, 1.f, GLFW_KEY_RIGHT_SHIFT);
    l_add(0x26, "Up", 16.5f, l_y);
    l_add(0x61, "1", 19.f, l_y);
    l_add(0x62, "2", 20.f, l_y);
    l_add(0x63, "3", 21.f, l_y);
    l_add(0x0D, "Enter", 22.f, l_y, 1.f, 2.f, GLFW_KEY_KP_ENTER);

    // Bottom row
    l_y = 5.25f;
    l_add(0x11, "Ctrl", 0.f, l_y, 1.25f, 1.f, GLFW_KEY_LEFT_CONTROL);
    l_add(0x5B, "Win", 1.25f, l_y, 1.25f);
    l_add(0x12, "Alt", 2.5f, l_y, 1.25f, 1.f, GLFW_KEY_LEFT_ALT);
    l_add(0x20, "Space", 3.75f, l_y, 6.25f);
    l_add(0x12, "Alt", 10.f, l_y, 1.25f, 1.f, GLFW_KEY_RIGHT_ALT);
    l_add(0x5C, "Win", 11.25f, l_y, 1.25f);
    l_add(0x5D, "Menu", 12.5f, l_y, 1.25f);
    l_add(0x11, "Ctrl", 13.75f, l_y, 1.25f, 1.f, GLFW_KEY_RIGHT_CONTROL);
    l_add(0x25, "Left", 15.5f, l_y);
    l_add(0x28, "Down", 16.5f, l_y);
    l_add(0x27, "Right", 17.5f, l_y);
    l_add(0x60, "0", 19.f, l_y, 2.f);
    l_add(0x6E, ".", 21.f, l_y);

    // Mouse: left / wheel / right on top of the body, two side buttons
    l_add(0x01, "L", g_mouseLeft + 0.1f, g_mouseTop + 0.1f, 1.35f, 1.9f);
    l_add(0x04, "M", g_mouseLeft + 1.45f, g_mouseTop + 0.35f, 0.5f, 1.4f);
    l_add(0x02, "R", g_mouseLeft + 1.95f, g_mouseTop + 0.1f, 1.35f, 1.9f);
    l_add(0x05, "4", g_mouseLeft - 0.6f, g_mouseTop + 2.1f, 0.55f, 0.8f);
    l_add(0x06, "5", g_mouseLeft - 0.6f, g_mouseTop + 3.f, 0.55f, 0.8f);
    return l_caps;
}

ImU32 Lighten(const ImVec4 &f_color, float f_amount)
{
    return ImGui::GetColorU32(ImVec4(f_color.x + (1.f - f_color.x) * f_amount, f_color.y + (1.f - f_color.y) * f_amount, f_color.z + (1.f - f_color.z) * f_amount, 1.f));
}

// Lines of f_text centred horizontally, stacked upwards from the bottom of
// the rect; the font shrinks until the widest line fits
void DrawCapLabel(ImDrawList *f_list, ImFont *f_font, float f_size, const ImVec2 &f_min, const ImVec2 &f_max, float f_bottomPad, ImU32 f_color, const std::string &f_text)
{
    std::vector<std::string> l_lines;
    size_t l_start = 0U;
    for(;;)
    {
        const size_t l_break = f_text.find('\n', l_start);
        l_lines.push_back(f_text.substr(l_start, (l_break == std::string::npos) ? std::string::npos : (l_break - l_start)));
        if(l_break == std::string::npos) break;
        l_start = l_break + 1U;
    }
    const float l_available = (f_max.x - f_min.x) - 4.f;
    float l_widest = 0.f;
    for(const auto &l_line : l_lines) l_widest = std::max(l_widest, f_font->CalcTextSizeA(f_size, FLT_MAX, 0.f, l_line.c_str()).x);
    float l_size = f_size;
    if(l_widest > l_available) l_size = std::max(7.f, f_size * l_available / l_widest);

    const float l_lineHeight = l_size * 1.05f;
    float l_y = f_max.y - f_bottomPad - (l_lineHeight * l_lines.size());
    for(const auto &l_line : l_lines)
    {
        const float l_width = f_font->CalcTextSizeA(l_size, FLT_MAX, 0.f, l_line.c_str()).x;
        f_list->AddText(f_font, l_size, ImVec2((f_min.x + f_max.x) * 0.5f - l_width * 0.5f, l_y), f_color, l_line.c_str());
        l_y += l_lineHeight;
    }
}

float UnitFor(float f_width, float f_pad)
{
    return std::clamp((f_width - 2.f * f_pad) / g_layoutWidthUnits, 14.f, 38.f);
}

}

float CBindingsView::PreferredHeight(float f_width)
{
    const float l_pad = 8.f;
    return (g_keyboardHeightUnits + 0.3f + g_legendRowUnits) * UnitFor(f_width, l_pad) + 2.f * l_pad;
}

void CBindingsView::Draw(const BindingsViewInput &f_in, float f_width, float f_height)
{
    static const std::vector<KeyCap> s_layout = BuildLayout();

    ImDrawList *l_list = ImGui::GetWindowDrawList();
    ImFont *l_font = ImGui::GetFont();
    const ImVec2 l_origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(f_width, f_height));

    const float l_pad = 8.f;
    const float l_unit = UnitFor(f_width, l_pad);
    const float l_layoutWidth = g_layoutWidthUnits * l_unit;
    const float l_ox = l_origin.x + std::max(l_pad, (f_width - l_layoutWidth) * 0.5f);
    const float l_oy = l_origin.y + l_pad;
    const float l_gap = 0.06f * l_unit;
    const float l_rounding = 0.12f * l_unit;
    const float l_legendSize = std::max(7.f, 0.25f * l_unit);
    const float l_labelSize = std::max(7.f, 0.27f * l_unit);

    // Which actions sit on which key
    std::vector<int> l_byVk[256];
    if(f_in.m_bindings)
    {
        for(int i = 0; i < KB_Count; i++)
        {
            const int l_vk = f_in.m_bindings[i];
            if((l_vk > 0) && (l_vk < 256)) l_byVk[l_vk].push_back(i);
        }
    }
    const bool l_live = (f_in.m_input != nullptr) && f_in.m_focused;
    // Tooltips only from the panel itself: IsMouseHoveringRect ignores windows
    // on top, and a binding combo's list can open over the keyboard
    const bool l_hoverable = !f_in.m_captured && ImGui::IsWindowHovered();

    const ImU32 l_unboundFill = ImGui::GetColorU32(ImVec4(0.16f, 0.17f, 0.21f, 1.f));
    const ImU32 l_unboundBorder = ImGui::GetColorU32(ImVec4(0.27f, 0.28f, 0.34f, 1.f));
    const ImU32 l_legendDim = ImGui::GetColorU32(ImVec4(0.55f, 0.57f, 0.63f, 1.f));
    const ImU32 l_legendBright = ImGui::GetColorU32(ImVec4(0.95f, 0.95f, 0.97f, 1.f));
    const ImU32 l_white = ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 1.f));

    // Mouse body first, its buttons are caps drawn with the rest
    {
        const ImVec2 l_min(l_ox + g_mouseLeft * l_unit, l_oy + g_mouseTop * l_unit);
        const ImVec2 l_max(l_min.x + g_mouseWidth * l_unit, l_min.y + g_mouseHeight * l_unit);
        l_list->AddRectFilled(l_min, l_max, ImGui::GetColorU32(ImVec4(0.13f, 0.14f, 0.18f, 1.f)), 1.1f * l_unit);
        l_list->AddRect(l_min, l_max, l_unboundBorder, 1.1f * l_unit);
        // The motion itself: looks around, or aims the active controller
        l_list->AddText(l_font, l_legendSize, ImVec2(l_min.x + 0.35f * l_unit, l_min.y + 2.35f * l_unit), l_legendDim, "move");
        const ImVec4 &l_fixed = g_categoryColors[C_Fixed];
        const ImU32 l_motionColor = f_in.m_captured ? Lighten(l_fixed, 0.5f) : ImGui::GetColorU32(ImVec4(l_fixed.x + 0.25f, l_fixed.y + 0.25f, l_fixed.z + 0.25f, 1.f));
        DrawCapLabel(l_list, l_font, l_labelSize, ImVec2(l_min.x, l_min.y + 2.4f * l_unit), ImVec2(l_max.x, l_max.y), 0.35f * l_unit, l_motionColor,
            f_in.m_mouseAim ? "aims the\ncontroller" : "looks\naround");
    }

    for(const KeyCap &l_cap : s_layout)
    {
        const ImVec2 l_min(l_ox + l_cap.m_x * l_unit + l_gap, l_oy + l_cap.m_y * l_unit + l_gap);
        const ImVec2 l_max(l_ox + (l_cap.m_x + l_cap.m_w) * l_unit - l_gap, l_oy + (l_cap.m_y + l_cap.m_h) * l_unit - l_gap);
        const std::vector<int> &l_bound = l_byVk[l_cap.m_vk & 0xFF];
        const bool l_escape = (l_cap.m_vk == 0x1B) && l_bound.empty();
        const bool l_pressed = l_live && ((l_cap.m_glfwKey != GLFW_KEY_UNKNOWN) ? f_in.m_input->IsGlfwKeyDown(l_cap.m_glfwKey) : f_in.m_input->IsVkDown(l_cap.m_vk));

        ImU32 l_fill = l_unboundFill;
        ImU32 l_border = l_unboundBorder;
        ImU32 l_legendColor = l_legendDim;
        std::string l_label;
        if(!l_bound.empty() || l_escape)
        {
            const ImVec4 &l_color = g_categoryColors[l_escape ? C_Fixed : CategoryOf(l_bound.front())];
            l_fill = l_pressed ? Lighten(l_color, 0.35f) : ImGui::GetColorU32(l_color);
            l_border = l_pressed ? l_white : Lighten(l_color, 0.25f);
            l_legendColor = l_legendBright;
            l_label = l_escape ? "release\nmouse" : ShortLabel(l_bound.front(), f_in.m_activeLeft);
            if(l_bound.size() > 1U) l_label += " +" + std::to_string(l_bound.size() - 1U);
        }
        else if(l_pressed)
        {
            l_fill = ImGui::GetColorU32(ImVec4(0.30f, 0.31f, 0.37f, 1.f));
            l_border = l_white;
        }

        l_list->AddRectFilled(l_min, l_max, l_fill, l_rounding);
        l_list->AddRect(l_min, l_max, l_border, l_rounding, 0, l_pressed ? 2.f : 1.f);
        l_list->AddText(l_font, l_legendSize, ImVec2(l_min.x + 0.1f * l_unit, l_min.y + 0.06f * l_unit), l_legendColor, l_cap.m_legend);
        if(!l_label.empty()) DrawCapLabel(l_list, l_font, l_labelSize, l_min, l_max, 0.08f * l_unit, l_white, l_label);

        if(l_hoverable && ImGui::IsMouseHoveringRect(l_min, l_max))
        {
            std::string l_tip(GetKeyName(l_cap.m_vk));
            if(l_tip == "(unknown)") l_tip = l_cap.m_legend;
            if(l_escape) l_tip += "\nReleases the mouse (fixed)";
            else if(l_bound.empty()) l_tip += "\nNot bound - rebind in Settings";
            for(int l_binding : l_bound)
            {
                l_tip += "\n";
                l_tip += g_keyBindingDefs[l_binding].m_label;
            }
            ImGui::SetTooltip("%s", l_tip.c_str());
        }
    }

    // Colour legend and the live mode line
    const float l_legendY = l_oy + (g_keyboardHeightUnits + 0.3f) * l_unit;
    float l_x = l_ox;
    for(int i = 0; i < C_Count; i++)
    {
        const float l_swatch = 0.5f * l_unit;
        const float l_top = l_legendY + (g_legendRowUnits * l_unit - l_swatch) * 0.5f;
        l_list->AddRectFilled(ImVec2(l_x, l_top), ImVec2(l_x + l_swatch, l_top + l_swatch), ImGui::GetColorU32(g_categoryColors[i]), 2.f);
        const float l_textWidth = l_font->CalcTextSizeA(l_legendSize, FLT_MAX, 0.f, g_categoryNames[i]).x;
        l_list->AddText(l_font, l_legendSize, ImVec2(l_x + l_swatch + 0.15f * l_unit, l_top + (l_swatch - l_legendSize) * 0.5f), l_legendBright, g_categoryNames[i]);
        l_x += l_swatch + 0.15f * l_unit + l_textWidth + 0.45f * l_unit;
    }

    // Live mode line, right-aligned after the legend; shorter wordings when the
    // row is narrow so it never runs off the panel
    const char *l_hand = f_in.m_activeLeft ? "LEFT" : "RIGHT";
    const char *l_mouse = f_in.m_mouseAim ? "aims the controller" : "looks around";
    const char *l_capture = f_in.m_captured ? "Esc releases the mouse" : "Click the view to capture the mouse";
    const std::string l_wordings[3] =
    {
        std::string("Active hand: ") + l_hand + "   Mouse: " + l_mouse + "   " + l_capture,
        std::string("Hand: ") + l_hand + "   Mouse: " + (f_in.m_mouseAim ? "aim" : "look") + "   " + (f_in.m_captured ? "Esc releases" : "Click to capture"),
        std::string("Hand: ") + l_hand + "   Mouse: " + (f_in.m_mouseAim ? "aim" : "look"),
    };
    const float l_modeRoom = (l_ox + l_layoutWidth) - l_x;
    for(const std::string &l_mode : l_wordings)
    {
        const float l_modeWidth = l_font->CalcTextSizeA(l_legendSize, FLT_MAX, 0.f, l_mode.c_str()).x;
        if((l_modeWidth > l_modeRoom) && (&l_mode != &l_wordings[2])) continue;
        l_list->AddText(l_font, l_legendSize, ImVec2(std::max(l_x, l_ox + l_layoutWidth - l_modeWidth), l_legendY + (g_legendRowUnits * l_unit - l_legendSize) * 0.5f), l_legendBright, l_mode.c_str());
        break;
    }
}
