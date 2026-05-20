// mr_vla_top_single.sv — T0033 variant_single.
// All three modality pipelines run ungated (rate enables tied high) — the
// "GPU-style pay-the-fastest-stream-cost" baseline. Identical datapath to the
// T0030 multi-rate scheduler; ONLY the rate-enable wiring differs.

`default_nettype none

module mr_vla_top_single (
    input  wire logic        clk,
    input  wire logic        rst_n,
    input  wire logic [31:0] s_vision_tdata,
    input  wire logic        s_vision_tvalid,
    output      logic        s_vision_tready,
    input  wire logic [31:0] s_language_tdata,
    input  wire logic        s_language_tvalid,
    output      logic        s_language_tready,
    output      logic [31:0] m_action_tdata,
    output      logic        m_action_tvalid,
    input  wire logic        m_action_tready,
    output      logic [31:0] mac_vision_acc_dbg,
    output      logic [31:0] mac_xmod_acc_dbg,
    output      logic [31:0] mac_gvsa_acc_dbg,
    output      logic [31:0] mac_action_acc_dbg
);
    // *** variant_single: enables tied high — no rate gating ***
    logic vision_en, language_en, action_en;
    assign vision_en   = 1'b1;
    assign language_en = 1'b1;
    assign action_en   = 1'b1;

    logic [31:0] vision_fifo_q;
    logic        vision_fifo_valid, vision_consume;
    bram_axis_fifo #(.WIDTH(32), .DEPTH(16384)) u_vision_fifo (
        .clk(clk), .rst_n(rst_n),
        .s_axis_tdata(s_vision_tdata), .s_axis_tvalid(s_vision_tvalid), .s_axis_tready(s_vision_tready),
        .m_axis_tdata(vision_fifo_q), .m_axis_tvalid(vision_fifo_valid), .m_axis_tready(vision_consume)
    );
    assign vision_consume = vision_en && vision_fifo_valid;
    logic [31:0] vision_acc;
    mac_unit u_vision_encoder_stub (
        .clk(clk), .rst_n(rst_n), .en(vision_consume),
        .a(vision_fifo_q), .b(32'h0000_0001), .acc(vision_acc)
    );
    assign mac_vision_acc_dbg = vision_acc;

    logic [31:0] lang_fifo_q;
    logic        lang_fifo_valid, lang_consume;
    bram_axis_fifo #(.WIDTH(32), .DEPTH(8192)) u_language_fifo (
        .clk(clk), .rst_n(rst_n),
        .s_axis_tdata(s_language_tdata), .s_axis_tvalid(s_language_tvalid), .s_axis_tready(s_language_tready),
        .m_axis_tdata(lang_fifo_q), .m_axis_tvalid(lang_fifo_valid), .m_axis_tready(lang_consume)
    );
    assign lang_consume = language_en && lang_fifo_valid;
    logic [31:0] gvsa_acc;
    mac_unit u_gvsa_matmul_stub (
        .clk(clk), .rst_n(rst_n), .en(lang_consume),
        .a(lang_fifo_q), .b(vision_acc), .acc(gvsa_acc)
    );
    assign mac_gvsa_acc_dbg = gvsa_acc;

    logic [31:0] xmod_acc;
    mac_unit u_xmod_attn_stub (
        .clk(clk), .rst_n(rst_n), .en(action_en),
        .a(vision_acc), .b(gvsa_acc), .acc(xmod_acc)
    );
    assign mac_xmod_acc_dbg = xmod_acc;

    logic [31:0] xmod_fifo_q;
    logic        xmod_fifo_valid, xmod_consume, xmod_fifo_tready;
    bram_axis_fifo #(.WIDTH(32), .DEPTH(2048)) u_xmod_action_fifo (
        .clk(clk), .rst_n(rst_n),
        .s_axis_tdata(xmod_acc), .s_axis_tvalid(action_en), .s_axis_tready(xmod_fifo_tready),
        .m_axis_tdata(xmod_fifo_q), .m_axis_tvalid(xmod_fifo_valid), .m_axis_tready(xmod_consume)
    );
    assign xmod_consume = action_en && xmod_fifo_valid;
    logic [31:0] action_acc;
    mac_unit u_action_diff_stub (
        .clk(clk), .rst_n(rst_n), .en(xmod_consume),
        .a(xmod_fifo_q), .b(32'h0000_0001), .acc(action_acc)
    );
    assign mac_action_acc_dbg = action_acc;
    assign m_action_tdata  = action_acc;
    assign m_action_tvalid = action_en;
endmodule

`default_nettype wire
