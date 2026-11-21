#pragma once

#include "gpu2d.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TEX_WIDTH(teximage) (8u << (((teximage) >> 20) & 7))
#define TEX_HEIGHT(teximage) (8u << (((teximage) >> 23) & 7))

// Decodes the texture described by TEXIMAGE_PARAM / PLTT_BASE into out (width * height RGBA, alpha 0..31 in bits
// 24..31). Returns false for "no texture".
bool tex_decode(const VramMap *map, const uint8_t *vram, uint32_t teximage, uint32_t pltt, uint32_t *out);

#ifdef __cplusplus
}
#endif
