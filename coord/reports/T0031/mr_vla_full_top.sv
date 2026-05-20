// mr_vla_full_top.sv — MR-VLA full-system integration top for T0031.
//
// Integrates the four REAL compute IPs (HLS-generated RTL) under the
// single-PLL + three-rate-enable multi-rate scheduler topology:
//
//   mr_vla_full_top
//     +- rate_enable_gen        (vision/language/action clock-enables; from T0021)
//     +- 3x bram_axis_fifo      (cross-rate sync FIFOs: 64/32/8 KB; from T0030)
//     +- vit_encoder_12layer    (T0029 real HLS IP — PL-only DinoSigLIP-S encoder)
//     +- xmod_attn_v1           (T0024 real HLS IP — cross-modal sparse attention)
//     +- gvsa_matmul            (T0025 real HLS IP — Llama-1B GVSA matmul tile)
//     +- action_diff_head       (T0026 real HLS IP — ManiFlow 1-step action head)
//
// INTEGRATION METHOD (v0):
//   Each HLS IP top is instantiated with (* dont_touch *) so Vivado synthesis
//   keeps its full logic even though the ap_memory data ports are left
//   unconnected at this level. Only the ap_ctrl_hs handshake (ap_clk / ap_rst /
//   ap_start / ap_done / ap_idle / ap_ready) is wired. ap_start of each IP is
//   gated by its rate enable, so the scheduler topology is exercised.
//   This produces a HONEST combined post-synth resource + timing report
//   (paper Table V) — the four IPs' real logic is all present on one chip.
//
//   What v0 does NOT do (documented for orch, candidate v1 work):
//     - functional dataflow wiring of the ap_memory ports between IPs and FIFOs
//       (needs per-array AXIS<->ap_memory adapters; hundreds of ports)
//     - ps_axi_bridge: the PS<->PL bridge is a Versal CIPS/NoC hard block,
//       not synthesizable PL RTL — omitted from this PL-only synth.
//
// Clock: single 250 MHz primary. Reset: async active-low rst_n → active-high
// ap_rst for the HLS IPs.

`default_nettype none

module mr_vla_full_top (
    input  wire logic        clk,
    input  wire logic        rst_n,

    // External streaming I/O (vision in, action out) — kept so synth has real
    // top-level ports and does not prune the FIFO chain.
    input  wire logic [31:0] s_vision_tdata,
    input  wire logic        s_vision_tvalid,
    output      logic        s_vision_tready,
    input  wire logic [31:0] s_language_tdata,
    input  wire logic        s_language_tvalid,
    output      logic        s_language_tready,
    output      logic [31:0] m_action_tdata,
    output      logic        m_action_tvalid,
    input  wire logic        m_action_tready,

    // IP status telemetry (promoted so the IPs' ap_done etc. are "used")
    output      logic        vit_done,
    output      logic        xmod_done,
    output      logic        gvsa_done,
    output      logic        adh_done,
    output      logic        vit_idle,
    output      logic        xmod_idle,
    output      logic        gvsa_idle,
    output      logic        adh_idle
);
    wire logic ap_rst = ~rst_n;

    // ---------------- Rate enables (single PLL, three sub-rates) -------------
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

    // ---------------- Cross-rate AXI-Stream FIFOs ---------------------------
    // vision -> xmod_attn (64 KB), language -> xmod_attn (32 KB),
    // xmod_attn -> action (8 KB). Kept structurally so their BRAM shows in the
    // integrated utilisation. m_axis read sides loop back to telemetry.
    logic [31:0] vfifo_q, lfifo_q, xfifo_q;
    logic        vfifo_v, lfifo_v, xfifo_v;
    logic        vfifo_rdy, lfifo_rdy, xfifo_rdy;

    bram_axis_fifo #(.WIDTH(32), .DEPTH(16384)) u_vision_fifo (
        .clk(clk), .rst_n(rst_n),
        .s_axis_tdata (s_vision_tdata), .s_axis_tvalid(s_vision_tvalid), .s_axis_tready(s_vision_tready),
        .m_axis_tdata (vfifo_q), .m_axis_tvalid(vfifo_v), .m_axis_tready(vision_en)
    );
    bram_axis_fifo #(.WIDTH(32), .DEPTH(8192)) u_language_fifo (
        .clk(clk), .rst_n(rst_n),
        .s_axis_tdata (s_language_tdata), .s_axis_tvalid(s_language_tvalid), .s_axis_tready(s_language_tready),
        .m_axis_tdata (lfifo_q), .m_axis_tvalid(lfifo_v), .m_axis_tready(language_en)
    );
    bram_axis_fifo #(.WIDTH(32), .DEPTH(2048)) u_xmod_action_fifo (
        .clk(clk), .rst_n(rst_n),
        .s_axis_tdata (xfifo_q), .s_axis_tvalid(action_en), .s_axis_tready(xfifo_rdy),
        .m_axis_tdata (m_action_tdata), .m_axis_tvalid(m_action_tvalid), .m_axis_tready(m_action_tready)
    );
    assign xfifo_q = {vfifo_q[15:0], lfifo_q[15:0]};  // token mixing placeholder

    // ---------------- Real compute IPs (HLS RTL, dont_touch) ----------------
    // Each IP's ap_start is gated by its rate enable. Data (ap_memory) ports
    // are intentionally left unconnected for this structural integration synth;
    // dont_touch keeps the full IP logic so the resource report is accurate.

    (* dont_touch = "yes" *) vit_encoder_12layer u_vit_encoder (
        .ap_clk  (clk),
        .ap_rst  (ap_rst),
        .ap_start(vision_en),
        .ap_done (vit_done),
        .ap_idle (vit_idle),
        .ap_ready()
    );

    (* dont_touch = "yes" *) xmod_attn_v1 u_xmod_attn (
        .ap_clk  (clk),
        .ap_rst  (ap_rst),
        .ap_start(action_en),
        .ap_done (xmod_done),
        .ap_idle (xmod_idle),
        .ap_ready()
    );

    (* dont_touch = "yes" *) gvsa_matmul u_gvsa_matmul (
        .ap_clk  (clk),
        .ap_rst  (ap_rst),
        .ap_start(language_en),
        .ap_done (gvsa_done),
        .ap_idle (gvsa_idle),
        .ap_ready()
    );

    (* dont_touch = "yes" *) action_diff_head u_action_diff (
        .ap_clk  (clk),
        .ap_rst  (ap_rst),
        .ap_start(action_en),
        .ap_done (adh_done),
        .ap_idle (adh_idle),
        .ap_ready()
    );

endmodule

`default_nettype wire
