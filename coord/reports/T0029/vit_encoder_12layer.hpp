// vit_encoder_12layer.hpp — full DinoSigLIP-S ViT encoder (12 transformer layers).
// Time-multiplexed: ONE physical vit_transformer_layer instance, reused 12×.

#ifndef VIT_ENCODER_12LAYER_HPP
#define VIT_ENCODER_12LAYER_HPP

#include <stdint.h>
#include "vit_transformer_layer.hpp"   // brings N_PATCH, D, D_QKV, D_FFN, K_UR

#define NLAYER 12

void vit_encoder_12layer(
    const int8_t x          [N_PATCH][D],
    const int8_t W_qkv_all  [NLAYER][D][D_QKV],
    const int8_t W_out_all  [NLAYER][D][D],
    const int8_t W_ffn1_all [NLAYER][D][D_FFN],
    const int8_t W_ffn2_all [NLAYER][D_FFN][D],
    int8_t       out        [N_PATCH][D]
);

#endif
