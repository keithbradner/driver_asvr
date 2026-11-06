#pragma once

class CInputController;

// The key-binding panel at the bottom of the window: a keyboard and a mouse
// drawn with the ImGui draw list, every bound key filled in the colour of what
// it does and labelled with it, keys that are held lit up, the mouse buttons
// and the mouse's own job (look / aim) shown the same way, and a colour legend.
struct BindingsViewInput
{
    const int *m_bindings = nullptr;           // KB_Count Windows VK codes
    const CInputController *m_input = nullptr; // live key state; may be null
    bool m_activeLeft = false;                 // A/B read X/Y on the left controller
    bool m_mouseAim = false;
    bool m_captured = false;
    bool m_focused = false;
};

class CBindingsView final
{
public:
    // Height the panel wants for a given width (keyboard + legend row)
    static float PreferredHeight(float f_width);
    // Draws at the current cursor of the current ImGui window, f_width x f_height
    void Draw(const BindingsViewInput &f_in, float f_width, float f_height);
};
