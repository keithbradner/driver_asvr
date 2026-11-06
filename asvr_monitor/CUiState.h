#pragma once

// What the window remembers between runs: which panels were open and how big
// it was. Stored as asvr_monitor.json in CPlatform::GetStateDirectory().
struct UiState
{
    bool m_settingsOpen = true;  // the hamburger panel; open on a fresh install so Install is in view
    bool m_bindingsOpen = true;  // the key-binding keyboard at the bottom
    int m_windowWidth = 0;       // 0: pick a default for the screen
    int m_windowHeight = 0;
};

UiState LoadUiState();
void SaveUiState(const UiState &f_state);
