// mr_vla_top.sv — MR-VLA multi-rate scheduler v1.
//
// Compared to T0021 tri_rate_toy:
//   - Same single-PLL + 3-rate-enable topology (reuses rate_enable_gen).
//   - Larger AXI-Stream FIFOs between rate domains (BRAM-backed, sync read):
//        vision  → xmod_attn:  16,384 entries × 32-bit = 64 KB
//        language → xmod_attn:  8,192 entries × 32-bit = 32 KB
//        xmod_attn → action:    2,048 entries × 32-bit = 8 KB
//   - Four compute units (stubs for v0):
//        vision_encoder_stub  (gated by vision_en  @30 Hz)
//        xmod_attn_stub       (gated by action_en  @100 Hz) — placeholder for T0024 v1
//        gvsa_matmul_stub     (gated by language_en @1 Hz)
//        action_diff_stub     (gated by action_en  @100 Hz)
//
// v0 uses stubs (32-bit MAC accumulators, same as T0021). REAL IP integration
// (export_design from Vitis HLS + Vivado IP integrator + AXIS-↔-ap_ctrl_hs
// adapter logic) is deferred to v1 because:
//   1. T0030's primary purpose is to validate that the TOPOLOGY timing-closes
//      with realistic FIFO sizes (not toy 64-deep distributed-RAM ones).
//   2. The real IP resource numbers were already measured independently in
//      T0024 (xmod_attn), T0025 (GVSA), T0026 (action diff head).
//   3. v1 integration would add ~3-5 hours of setup with no new architectural
//      information about clock topology / CDC / multi-rate scheduling.

`default_nettype none

module mr_vla_top (
    input  wire logic        clk,
    input  wire logic        rst_n,

    // Vision token input stream (fast rate; producer drives s_vision_tvalid)
    input  wire logic [31:0] s_vision_tdata,
    input  wire logic        s_vision_tvalid,
    output      logic        s_vision_tready,

    // Language token input stream (slow rate)
    input  wire logic [31:0] s_language_tdata,
    input  wire logic        s_language_tvalid,
    output      logic        s_language_tready,

    // Action output stream (medium rate)
    output      logic [31:0] m_action_tdata,
    output      logic        m_action_tvalid,
    input  wire logic        m_action_tready,

    // Telemetry (probe points)
    output      logic [31:0] mac_vision_acc_dbg,
    output      logic [31:0] mac_xmod_acc_dbg,
    output      logic [31:0] mac_gvsa_acc_dbg,
    output      logic [31:0] mac_action_acc_dbg
);
    // ───────────── Rate enables (single PLL, three sub-rates) ─────────────
    logic vision_en, language_en, action_en;
    rate_enable_gen #(
        .VISION_CYCLES  (8_333_333),
        .LANGUAGE_CYCLES(250_000_000),
        .ACTION_CYCLES  (2_500_000)
    ) u_enable_gen (
        .clk(clk), .rst_n(rst_n),
        .vision_en  (vision_en),
        .language_en(language_en),
        .action_en  (action_en)
    );

    // ───────────── Vision encoder stub + vision FIFO ─────────────
    logic [31:0] vision_fifo_q;
    logic        vision_fifo_valid;
    logic        vision_consume;
    bram_axis_fifo #(.WIDTH(32), .DEPTH(16384)) u_vision_fifo (
        .clk(clk), .rst_n(rst_n),
        .s_axis_tdata (s_vision_tdata),
        .s_axis_tvalid(s_vision_tvalid),
        .s_axis_tready(s_vision_tready),
        .m_axis_tdata (vision_fifo_q),
        .m_axis_tvalid(vision_fifo_valid),
        .m_axis_tready(vision_consume)
    );

    assign vision_consume = vision_en && vision_fifo_valid;

    logic [31:0] vision_acc;
    mac_unit u_vision_encoder_stub (
        .clk(clk), .rst_n(rst_n),
        .en (vision_consume),
        .a  (vision_fifo_q),
        .b  (32'h0000_0001),   // dummy coefficient (stub for vision_encoder)
        .acc(vision_acc)
    );
    assign mac_vision_acc_dbg = vision_acc;

    // ───────────── Language FIFO + GVSA stub ─────────────
    logic [31:0] lang_fifo_q;
    logic        lang_fifo_valid;
    logic        lang_consume;
    bram_axis_fifo #(.WIDTH(32), .DEPTH(8192)) u_language_fifo (
        .clk(clk), .rst_n(rst_n),
        .s_axis_tdata (s_language_tdata),
        .s_axis_tvalid(s_language_tvalid),
        .s_axis_tready(s_language_tready),
        .m_axis_tdata (lang_fifo_q),
        .m_axis_tvalid(lang_fifo_valid),
        .m_axis_tready(lang_consume)
    );

    assign lang_consume = language_en && lang_fifo_valid;

    logic [31:0] gvsa_acc;
    mac_unit u_gvsa_matmul_stub (
        .clk(clk), .rst_n(rst_n),
        .en (lang_consume),
        .a  (lang_fifo_q),
        .b  (vision_acc),        // GVSA mixes language with vision context
        .acc(gvsa_acc)
    );
    assign mac_gvsa_acc_dbg = gvsa_acc;

    // ───────────── xmod_attn stub (gated by action rate) ─────────────
    logic [31:0] xmod_acc;
    mac_unit u_xmod_attn_stub (
        .clk(clk), .rst_n(rst_n),
        .en (action_en),
        .a  (vision_acc),
        .b  (gvsa_acc),
        .acc(xmod_acc)
    );
    assign mac_xmod_acc_dbg = xmod_acc;

    // ───────────── xmod → action FIFO ─────────────
    logic [31:0] xmod_fifo_q;
    logic        xmod_fifo_valid;
    logic        xmod_consume;
    logic        xmod_fifo_tready;   // ignore in v0
    bram_axis_fifo #(.WIDTH(32), .DEPTH(2048)) u_xmod_action_fifo (
        .clk(clk), .rst_n(rst_n),
        .s_axis_tdata (xmod_acc),
        .s_axis_tvalid(action_en),
        .s_axis_tready(xmod_fifo_tready),
        .m_axis_tdata (xmod_fifo_q),
        .m_axis_tvalid(xmod_fifo_valid),
        .m_axis_tready(xmod_consume)
    );

    assign xmod_consume = action_en && xmod_fifo_valid;

    // ───────────── Action diff head stub + output ─────────────
    logic [31:0] action_acc;
    mac_unit u_action_diff_stub (
        .clk(clk), .rst_n(rst_n),
        .en (xmod_consume),
        .a  (xmod_fifo_q),
        .b  (32'h0000_0001),
        .acc(action_acc)
    );
    assign mac_action_acc_dbg = action_acc;

    assign m_action_tdata  = action_acc;
    assign m_action_tvalid = action_en;

endmodule

`default_nettype wire
