// LineTracer - status/label text and control enable state.
#pragma once
#include <string>

#include "app_state.h"

namespace app {

void updateTimeLabel();
void updateStats();
void updateInfo();
void setStatus(const std::wstring& s);
void setSliderFromTime();
void setPlayButton();
void updateMonitorEnable();
std::wstring hotkeyName(unsigned mods, unsigned vk);
void setRecordLabel();
void enableControlsForRecording(bool rec);

}  // namespace app
