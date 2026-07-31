#pragma once

// Monitor key bindings, stored as Windows virtual-key codes in the
// driver_asvr section of steamvr.vrsettings (defaults also in
// resources/settings/default.vrsettings). The Config tool edits them; the
// monitor reads them at startup.

constexpr char g_driverSettingsSection[] = "driver_asvr";

enum EKeyBinding : int
{
    KB_MoveForward = 0,
    KB_MoveBackward,
    KB_MoveLeft,
    KB_MoveRight,
    KB_MoveUp,
    KB_MoveDown,
    KB_YawLeft,
    KB_YawRight,
    KB_PitchUp,
    KB_PitchDown,
    KB_RollLeft,
    KB_RollRight,
    KB_SystemButton,
    // Controller actions target the ACTIVE controller (right by default);
    // KB_SwitchController toggles between right and left
    KB_Trigger,
    KB_Grip,
    KB_SwitchController,
    KB_MouseAim, // toggle: mouse rotates the active controller instead of the head
    KB_ButtonA, // A on the right controller, X on the left
    KB_ButtonB, // B on the right controller, Y on the left
    KB_ControllerForward,
    KB_ControllerBackward,
    KB_ControllerLeft,
    KB_ControllerRight,
    KB_JoystickUp,
    KB_JoystickDown,
    KB_JoystickLeft,
    KB_JoystickRight,

    KB_Count
};

struct KeyBindingDef
{
    const char *m_settingsKey;
    const char *m_label;
    int m_defaultKey; // Windows VK code
};

constexpr KeyBindingDef g_keyBindingDefs[KB_Count]
{
    { "keyMoveForward", "Move forward", 0x57 /* W */ },
    { "keyMoveBackward", "Move backward", 0x53 /* S */ },
    { "keyMoveLeft", "Move left", 0x41 /* A */ },
    { "keyMoveRight", "Move right", 0x44 /* D */ },
    { "keyMoveUp", "Move up", 0x21 /* Page Up */ },
    { "keyMoveDown", "Move down", 0x22 /* Page Down */ },
    { "keyYawLeft", "Yaw left", 0x64 /* Numpad 4 */ },
    { "keyYawRight", "Yaw right", 0x66 /* Numpad 6 */ },
    { "keyPitchUp", "Pitch up", 0x68 /* Numpad 8 */ },
    { "keyPitchDown", "Pitch down", 0x62 /* Numpad 2 */ },
    { "keyRollLeft", "Roll left", 0x61 /* Numpad 1 */ },
    { "keyRollRight", "Roll right", 0x63 /* Numpad 3 */ },
    { "keySystemButton", "System button", 0x20 /* Space */ },
    { "keyTrigger", "Trigger", 0x01 /* Left Mouse */ },
    { "keyGrip", "Grip", 0x02 /* Right Mouse */ },
    { "keySwitchController", "Switch controller", 0x09 /* Tab */ },
    { "keyMouseAim", "Mouse aims controller (toggle)", 0x08 /* Backspace */ },
    { "keyButtonA", "A button (X on left)", 0x4E /* N */ },
    { "keyButtonB", "B button (Y on left)", 0x4D /* M */ },
    { "keyControllerForward", "Controller forward", 0x49 /* I */ },
    { "keyControllerBackward", "Controller backward", 0x4B /* K */ },
    { "keyControllerLeft", "Controller left", 0x4A /* J */ },
    { "keyControllerRight", "Controller right", 0x4C /* L */ },
    { "keyJoystickUp", "Joystick up", 0x26 /* Up Arrow */ },
    { "keyJoystickDown", "Joystick down", 0x28 /* Down Arrow */ },
    { "keyJoystickLeft", "Joystick left", 0x25 /* Left Arrow */ },
    { "keyJoystickRight", "Joystick right", 0x27 /* Right Arrow */ },
};
