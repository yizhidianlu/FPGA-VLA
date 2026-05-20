// divider_ip_stubs.v — behavioral replacements for the Xilinx Divider LogiCORE
// instances that the HLS-generated sdiv wrappers reference.
//
// The HLS softmax stages (NORM loop: weight = shifted*32767 / sum) infer a
// signed-divide, which Vitis HLS implements via a Xilinx Divider IP core.
// That core is delivered as an XCI/IP-generated netlist, NOT plain Verilog —
// so the raw HLS RTL alone is missing the `<top>_sdiv_..._ip` module.
//
// These stubs provide port-compatible behavioral dividers so the full-system
// integration (T0031) can synthesize. All three referenced cores have identical
// port widths (dividend 32-bit, divisor 24-bit, dout 56-bit):
//     <top>_sdiv_31ns_24s_16_19_1_ip   (vit_encoder, action_diff)
//     <top>_sdiv_31ns_22s_16_19_1_ip   (xmod_attn)
//
// dout layout (per the sdiv wrapper's slicing):
//     dout[54:24] = quotient (31 bits)   dout[23:0] = remainder
//
// CAVEAT: the real LogiCORE divider is a fully-pipelined radix-2 unit (19
// stages). These stubs do a combinational divide registered through a 19-deep
// shift pipeline — RESOURCE-representative but the divide path's timing is
// approximate. The standalone per-IP synths (T0024/T0026/T0029) used the REAL
// LogiCORE and all closed timing at Fmax > 340 MHz.

`timescale 1ns/1ps

// ---- shared behavioral divider body via a macro ----
`define BEHAV_DIV_BODY                                                       \
    localparam NS = 19;                                                       \
    reg [55:0] dpipe [0:NS-1];                                                \
    reg        vpipe [0:NS-1];                                                \
    integer i;                                                                \
    wire signed [31:0] num = s_axis_dividend_tdata;                           \
    wire signed [24:0] den = $signed({1'b0, s_axis_divisor_tdata});           \
    wire signed [31:0] q   = (den != 0) ? (num / den) : 32'sd0;               \
    wire        [23:0] r   = (den != 0) ? (num % den) : 24'd0;                \
    always @(posedge aclk) begin                                              \
        if (aclken) begin                                                     \
            dpipe[0] <= {1'b0, q[30:0], r[23:0]};                             \
            vpipe[0] <= s_axis_dividend_tvalid & s_axis_divisor_tvalid;        \
            for (i = 1; i < NS; i = i + 1) begin                              \
                dpipe[i] <= dpipe[i-1];                                        \
                vpipe[i] <= vpipe[i-1];                                        \
            end                                                               \
        end                                                                   \
    end                                                                       \
    assign m_axis_dout_tdata  = dpipe[NS-1];                                  \
    assign m_axis_dout_tvalid = vpipe[NS-1];

module vit_encoder_12layer_sdiv_31ns_24s_16_19_1_ip (
    input  wire        aclk,
    input  wire        aclken,
    input  wire        s_axis_dividend_tvalid,
    input  wire [31:0] s_axis_dividend_tdata,
    input  wire        s_axis_divisor_tvalid,
    input  wire [23:0] s_axis_divisor_tdata,
    output wire        m_axis_dout_tvalid,
    output wire [55:0] m_axis_dout_tdata
);
    `BEHAV_DIV_BODY
endmodule

module action_diff_head_sdiv_31ns_24s_16_19_1_ip (
    input  wire        aclk,
    input  wire        aclken,
    input  wire        s_axis_dividend_tvalid,
    input  wire [31:0] s_axis_dividend_tdata,
    input  wire        s_axis_divisor_tvalid,
    input  wire [23:0] s_axis_divisor_tdata,
    output wire        m_axis_dout_tvalid,
    output wire [55:0] m_axis_dout_tdata
);
    `BEHAV_DIV_BODY
endmodule

module xmod_attn_v1_sdiv_31ns_22s_16_19_1_ip (
    input  wire        aclk,
    input  wire        aclken,
    input  wire        s_axis_dividend_tvalid,
    input  wire [31:0] s_axis_dividend_tdata,
    input  wire        s_axis_divisor_tvalid,
    input  wire [23:0] s_axis_divisor_tdata,
    output wire        m_axis_dout_tvalid,
    output wire [55:0] m_axis_dout_tdata
);
    `BEHAV_DIV_BODY
endmodule
