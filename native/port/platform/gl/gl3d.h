#pragma once

#include "frame.h"

void gl3d_init(void);
// Renders the polygon list at (256 * wide * scale) x (192 * scale): `wide` > 1 for a list the game projected wider
// than 4:3. With `prev`, polygons found in both lists are drawn at fraction `t` from prev to frame (interpolated
// frames). Returns the colour texture (RGBA, alpha 0..1 as the DS 3D alpha).
uint32_t gl3d_render(const Gx3dFrame *frame, const Gx3dFrame *prev, float t, const FrameSnapshot *snap, int scale,
                     float wide);
// Which render target the next gl3d_render uses (0 or 1: one per screen).
void gl3d_use_target(int slot);
