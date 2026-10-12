// LineTracer - recording, playback, monitor choice and file open/save/export.
#pragma once
#include <filesystem>
#include <string>

#include "app_state.h"

namespace app {

size_t countAtTime(double t);  // samples visible at t ms into the recording
void populateMonitors(bool announce);
void onMonitorChanged();
void toggleRecording();
bool fileDialog(bool save, const wchar_t* filter, const wchar_t* defExt, std::wstring& out);
void loadFile(const std::filesystem::path& f);
void doExport();
void togglePlay();
void onTick();

}  // namespace app
