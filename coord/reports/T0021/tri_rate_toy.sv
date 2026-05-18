// tri_rate_toy.sv
// MR-VLA tri-rate scheduler — single 250 MHz PL clock domain, three sub-rate
// clock-enable pulses driving three MAC compute units, with AXI-Stream FIFOs
// between stages. This is the "honest" topology for H1 (three-rate scheduling)
// — proves that we DO NOT need three physical PLL clocks to get logical isolation.
//
// Stage layout:
//   external vision stream  -->  vision FIFO (depth 64)  -->  mac_vision (en: vision_en, 30 Hz)
//   external language stream -->  mac_attn   (en: language_en, 1 Hz)
//   mac_attn output         -->  attn-action FIFO (depth 8) --> mac_action (en: action_en, 100 Hz)

`default_nettype none

module tri_rate_toy #(
    parameter int VISION_FIFO_DEPTH = 64,
    parameter int ATTN_FIFO_DEPTH   = 8
) (
    input  wire logic        clk,
    input  wire logic        rst_n,

    // Vision input stream
    input  wire logic [31:0] s_vision_tdata,
    input  wire logic        s_vision_tvalid,
    output      logic        s_vision_tready,

    // Language input stream
    input  wire logic [31:0] s_language_tdata,
    input  wire logic        s_language_tvalid,
    output      logic        s_language_tready,

    // Action output stream
    output      logic [31:0] m_action_tdata,
    output      logic        m_action_tvalid,
    input  wire logic        m_action_tready,

    // Telemetry (helps debug, also forces the synthesizer to keep the MAC outputs)
    output      logic [31:0] mac_vision_acc_dbg,
    output      logic [31:0] mac_attn_acc_dbg,
    output      logic [31:0] mac_action_acc_dbg
);
    // ---------------- Rate enables (single PLL, three sub-rates) ----------------
    logic vision_en, language_en, action_en;

    rate_enable_gen #(
        .VISION_CYCLES  (8_333_333),     // ~30 Hz from 250 MHz
        .LANGUAGE_CYCLES(250_000_000),   // ~1  Hz
        .ACTION_CYCLES  (2_500_000)      // ~100 Hz
    ) u_enable_gen (
        .clk(clk), .rst_n(rst_n),
        .vision_en  (vision_en),
        .language_en(language_en),
        .action_en  (action_en)
    );

    // ---------------- Vision FIFO ----------------
    logic        vision_fifo_valid;
    logic [31:0] vision_fifo_q;
    logic        vision_consume;     // 1-cycle pop strobe = vision_en && have data

    axis_fifo #(.WIDTH(32), .DEPTH(VISION_FIFO_DEPTH)) u_vision_fifo (
        .clk(clk), .rst_n(rst_n),
        .s_axis_tdata (s_vision_tdata),
        .s_axis_tvalid(s_vision_tvalid),
        .s_axis_tready(s_vision_tready),
        .m_axis_tdata (vision_fifo_q),
        .m_axis_tvalid(vision_fifo_valid),
        .m_axis_tready(vision_consume)
    );

    assign vision_consume = vision_en && vision_fifo_valid;

    // ---------------- Vision MAC (30 Hz pulse) ----------------
    logic [31:0] mac_vision_acc;
    mac_unit u_mac_vision (
        .clk(clk), .rst_n(rst_n),
        .en (vision_consume),
        .a  (vision_fifo_q),
        .b  (32'h0000_0001),   // dummy coefficient
        .acc(mac_vision_acc)
    );
    assign mac_vision_acc_dbg = mac_vision_acc;

    // ---------------- Attention MAC (1 Hz, slowest) ----------------
    // Pulls language token directly (no FIFO needed at 1 Hz). Folds in vision state.
    assign s_language_tready = language_en;

    logic [31:0] mac_attn_acc;
    mac_unit u_mac_attn (
        .clk(clk), .rst_n(rst_n),
        .en (language_en),
        .a  (s_language_tvalid ? s_language_tdata : mac_vision_acc),
        .b  (mac_vision_acc),
        .acc(mac_attn_acc)
    );
    assign mac_attn_acc_dbg = mac_attn_acc;

    // ---------------- Attn -> Action FIFO ----------------
    logic        attn_fifo_q_valid;
    logic [31:0] attn_fifo_q;
    logic        attn_consume;
    logic        attn_fifo_tready_unused;

    axis_fifo #(.WIDTH(32), .DEPTH(ATTN_FIFO_DEPTH)) u_attn_action_fifo (
        .clk(clk), .rst_n(rst_n),
        .s_axis_tdata (mac_attn_acc),
        .s_axis_tvalid(language_en),     // push on every language MAC fire
        .s_axis_tready(attn_fifo_tready_unused),  // depth 8 vs 1 Hz writes -> never full
        .m_axis_tdata (attn_fifo_q),
        .m_axis_tvalid(attn_fifo_q_valid),
        .m_axis_tready(attn_consume)
    );

    assign attn_consume = action_en && attn_fifo_q_valid;

    // ---------------- Action MAC (100 Hz) ----------------
    logic [31:0] mac_action_acc;
    mac_unit u_mac_action (
        .clk(clk), .rst_n(rst_n),
        .en (attn_consume),
        .a  (attn_fifo_q),
        .b  (32'h0000_0001),
        .acc(mac_action_acc)
    );
    assign mac_action_acc_dbg = mac_action_acc;

    // Action output port
    assign m_action_tdata  = mac_action_acc;
    assign m_action_tvalid = action_en;

endmodule

`default_nettype wire
