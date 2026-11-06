#pragma once

// Human-readable names for the Windows virtual-key codes a monitor binding can
// hold. The settings panel's binding combos offer exactly this list, and the
// key-binding visualization labels keys with it.
struct SelectableKey
{
    int m_code;
    const char *m_name;
};

constexpr SelectableKey g_selectableKeys[]
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

inline const char* GetKeyName(int f_code)
{
    for(const auto &l_key : g_selectableKeys)
    {
        if(l_key.m_code == f_code) return l_key.m_name;
    }
    return "(unknown)";
}
