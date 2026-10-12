// LineTracer - main window: control creation, layout, font and slider handling.
#pragma once
#include "app_state.h"

namespace app {

void applyFont();       // current dpi -> ui font, and WM_SETFONT on the stock child controls
void layout();          // position every control for the current client size and dpi
void createControls();
void readSliders();     // sync brightness/gain/glow from the sliders and refresh their labels
void onHScroll(WPARAM w, LPARAM l);

}  // namespace app
