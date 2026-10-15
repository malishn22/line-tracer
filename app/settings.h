// LineTracer - settings persistence, the Settings dialog, hotkey handling and theme application.
#pragma once
#include "app_state.h"

namespace app {

void loadSettings();
void applySettingsToControls();  // call once the controls exist
void saveSettings();
void applyTheme();
bool registerRecordHotkey();
void openSettings();  // modal; returns when the dialog closes
LRESULT CALLBACK SettingsProc(HWND h, UINT m, WPARAM w, LPARAM l);

}  // namespace app
