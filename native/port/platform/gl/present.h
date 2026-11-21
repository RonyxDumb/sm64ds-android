#pragma once

#include "frame.h"

// Needs a current OpenGL ES 3 context.
void present_init(void);
// Takes a new snapshot (2D drawn now; 3D at 192*scale lines, drawn by present_draw).
void present_update(const FrameSnapshot *f, int scale);
// Draws the top and bottom screens into the rectangles (x, y, w, h each, y down) of a w x h framebuffer 0, each
// screen as large as fits. t in 0..1: how far between the previous and the last snapshot the 3D is drawn (1: the
// last one; less for frames in between at display rates above 60 Hz).
void present_draw(const int layout[8], int w, int h, float t);
// The framebuffer present_draw draws into, when it is not the window's (0): a headset's swapchain image.
void present_set_target(uint32_t framebuffer);
// Widescreen: the aspect ratio the race cameras use (guest_set_aspect); race frames are drawn that wide.
void present_set_wide(float aspect);
bool present_is_race(void); // the last snapshot was a race frame
bool present_engine_a_on_top(void);
