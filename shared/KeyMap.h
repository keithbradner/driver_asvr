#pragma once

// Windows virtual-key codes are the canonical persisted key-binding format on
// every platform (shared/KeyBindings.h, default.vrsettings, the Config tool's
// key list) so configs stay portable. The Linux monitor translates them to
// GLFW at load with this table.
//
// Mouse buttons (VK 0x01/0x02/0x04) are NOT translated here - they have no GLFW
// key equivalent; callers must route them to glfwGetMouseButton via
// KeyMapMouseButton() first.

#ifndef _WIN32
#include "GLFW/glfw3.h"

// GLFW mouse button for a VK code, or -1 when the code is not a mouse button
inline int KeyMapMouseButton(int f_vk)
{
    switch(f_vk)
    {
        case 0x01: return GLFW_MOUSE_BUTTON_LEFT;   // VK_LBUTTON
        case 0x02: return GLFW_MOUSE_BUTTON_RIGHT;  // VK_RBUTTON
        case 0x04: return GLFW_MOUSE_BUTTON_MIDDLE; // VK_MBUTTON
        default: return -1;
    }
}

// GLFW key for a VK code, or GLFW_KEY_UNKNOWN when unmapped (binding disabled)
inline int KeyMapToGlfw(int f_vk)
{
    // Letters and digits share their ASCII values
    if((f_vk >= 0x41) && (f_vk <= 0x5A)) return GLFW_KEY_A + (f_vk - 0x41);
    if((f_vk >= 0x30) && (f_vk <= 0x39)) return GLFW_KEY_0 + (f_vk - 0x30);
    if((f_vk >= 0x70) && (f_vk <= 0x87)) return GLFW_KEY_F1 + (f_vk - 0x70);      // VK_F1..F24
    if((f_vk >= 0x60) && (f_vk <= 0x69)) return GLFW_KEY_KP_0 + (f_vk - 0x60);    // VK_NUMPAD0..9

    switch(f_vk)
    {
        case 0x08: return GLFW_KEY_BACKSPACE;     // VK_BACK
        case 0x09: return GLFW_KEY_TAB;
        case 0x0D: return GLFW_KEY_ENTER;         // VK_RETURN
        case 0x10: return GLFW_KEY_LEFT_SHIFT;    // VK_SHIFT
        case 0x11: return GLFW_KEY_LEFT_CONTROL;  // VK_CONTROL
        case 0x12: return GLFW_KEY_LEFT_ALT;      // VK_MENU
        case 0x14: return GLFW_KEY_CAPS_LOCK;
        case 0x1B: return GLFW_KEY_ESCAPE;
        case 0x20: return GLFW_KEY_SPACE;
        case 0x21: return GLFW_KEY_PAGE_UP;       // VK_PRIOR
        case 0x22: return GLFW_KEY_PAGE_DOWN;     // VK_NEXT
        case 0x23: return GLFW_KEY_END;
        case 0x24: return GLFW_KEY_HOME;
        case 0x25: return GLFW_KEY_LEFT;
        case 0x26: return GLFW_KEY_UP;
        case 0x27: return GLFW_KEY_RIGHT;
        case 0x28: return GLFW_KEY_DOWN;
        case 0x2C: return GLFW_KEY_PRINT_SCREEN;  // VK_SNAPSHOT
        case 0x2D: return GLFW_KEY_INSERT;
        case 0x2E: return GLFW_KEY_DELETE;
        case 0x6A: return GLFW_KEY_KP_MULTIPLY;
        case 0x6B: return GLFW_KEY_KP_ADD;
        case 0x6D: return GLFW_KEY_KP_SUBTRACT;
        case 0x6E: return GLFW_KEY_KP_DECIMAL;
        case 0x6F: return GLFW_KEY_KP_DIVIDE;
        case 0xA0: return GLFW_KEY_LEFT_SHIFT;
        case 0xA1: return GLFW_KEY_RIGHT_SHIFT;
        case 0xA2: return GLFW_KEY_LEFT_CONTROL;
        case 0xA3: return GLFW_KEY_RIGHT_CONTROL;
        case 0xA4: return GLFW_KEY_LEFT_ALT;
        case 0xA5: return GLFW_KEY_RIGHT_ALT;
        case 0xBA: return GLFW_KEY_SEMICOLON;
        case 0xBB: return GLFW_KEY_EQUAL;
        case 0xBC: return GLFW_KEY_COMMA;
        case 0xBD: return GLFW_KEY_MINUS;
        case 0xBE: return GLFW_KEY_PERIOD;
        case 0xBF: return GLFW_KEY_SLASH;
        case 0xC0: return GLFW_KEY_GRAVE_ACCENT;
        case 0xDB: return GLFW_KEY_LEFT_BRACKET;
        case 0xDC: return GLFW_KEY_BACKSLASH;
        case 0xDD: return GLFW_KEY_RIGHT_BRACKET;
        case 0xDE: return GLFW_KEY_APOSTROPHE;
        default: return GLFW_KEY_UNKNOWN;
    }
}
#endif
