// vit_patch_embed.hpp — DinoSigLIP-S compatible patch embedding (PL-only).
// Input image 224×224×3 INT8 → 196 patches of 1024-dim INT8 features.

#ifndef VIT_PATCH_EMBED_HPP
#define VIT_PATCH_EMBED_HPP

#include <stdint.h>

#define IMG_H     224
#define IMG_W     224
#define CHANNELS    3
#define PATCH      16
#define NPATCH_Y  (IMG_H / PATCH)        // 14
#define NPATCH_X  (IMG_W / PATCH)        // 14
#define NPATCH    (NPATCH_Y * NPATCH_X)  // 196
#define PATCH_K   (CHANNELS * PATCH * PATCH)  // 768
#define EMBED_D   1024

void vit_patch_embed(
    const int8_t  img [IMG_H][IMG_W][CHANNELS],
    const int8_t  W   [PATCH_K][EMBED_D],
    const int32_t bias[EMBED_D],
    int8_t        out [NPATCH][EMBED_D]
);

#endif
