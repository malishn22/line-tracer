// LineTracer - the preview canvas: drawing state, painting, hover and click-to-seek.
#pragma once
#include "app_state.h"

namespace app {

void redrawAll();   // clear and re-render the whole path up to the playhead
void syncDraw();    // incremental forward, full redraw if the playhead moved back
void reconfigure(); // view mapping changed (bounds or canvas size)
void resizeCanvas();
LRESULT CALLBACK CanvasProc(HWND h, UINT m, WPARAM w, LPARAM l);

}  // namespace app
