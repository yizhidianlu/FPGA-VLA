// action_diff_head.hpp — 1-step ManiFlow flow-matching diffusion head for action regression.
// All INT8 quantization. ReLU instead of GeLU in v0 (cheaper, equivalent rank for HLS).

#ifndef ACTION_DIFF_HEAD_HPP
#define ACTION_DIFF_HEAD_HPP

#include <stdint.h>

#define C_DIM    256   // context / hidden dim
#define N_CTX    256   // number of context tokens
#define T_EMB    128   // timestep embedding dim
#define ACT_DIM    7   // 6 DoF + gripper
#define FFN_HID  512   // FFN hidden dim
#define K_UR      64   // k-unroll factor (matches T0025 pattern; HLS DSP58 packs 2 INT8/DSP)

// Top function
void action_diff_head(
    const int8_t context     [N_CTX][C_DIM],         // upstream conditioning
    const int8_t noisy_action[ACT_DIM],              // x_t
    const int8_t timestep_emb[T_EMB],                // sinusoidal embedding
    const int8_t W_time      [T_EMB][C_DIM],         // time embed MLP
    const int8_t W_act_proj  [ACT_DIM][C_DIM],       // action up-projection
    const int8_t W_ffn1      [C_DIM][FFN_HID],       // FFN gate
    const int8_t W_ffn2      [FFN_HID][C_DIM],       // FFN down
    const int8_t W_out       [C_DIM][ACT_DIM],       // output projection
    int8_t       out         [ACT_DIM]               // denoised action delta
);

#endif
