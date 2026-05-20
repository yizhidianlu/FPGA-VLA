// mr_vla_full_top.v — T0032 integration wrapper for bottom-up DCP linking.
//
// Instantiates the 5 MR-VLA components as black boxes (port lists supplied by
// the synth-stub .v files). After this wrapper is synthesized, the build_link
// TCL populates each black box with its real out-of-context .dcp netlist via
// `read_checkpoint -cell`, producing a genuine MEASURED integrated design for
// utilization + timing reporting.
//
//   u_scheduler   : mr_vla_top          (T0030 multi-rate scheduler RTL)
//   u_vit_encoder : vit_encoder_12layer (T0029)
//   u_xmod_attn   : xmod_attn_v1        (T0024)
//   u_gvsa_matmul : gvsa_matmul         (T0025)
//   u_action_diff : action_diff_head    (T0026)
//
// The 4 HLS IPs expose ap_ctrl_hs (ap_clk/ap_rst/ap_start/ap_done/ap_idle/
// ap_ready) plus many ap_memory data ports. Only the ap_ctrl handshake is
// wired here; the data ports stay unconnected — this is a structural
// integration whose purpose is the MEASURED combined resource + timing report
// (paper Table V [HW-EST]). Functional dataflow wiring is a separate concern.

`default_nettype none

module mr_vla_full_top (
    input  wire        clk,
    input  wire        rst_n,

    // per-IP start strobes (would be driven by the scheduler's rate enables
    // in the full system; exposed as ports so synth keeps them real)
    input  wire        vit_start,
    input  wire        xmod_start,
    input  wire        gvsa_start,
    input  wire        adh_start,

    // scheduler external streaming I/O
    input  wire [31:0] s_vision_tdata,
    input  wire        s_vision_tvalid,
    output wire        s_vision_tready,
    input  wire [31:0] s_language_tdata,
    input  wire        s_language_tvalid,
    output wire        s_language_tready,
    output wire [31:0] m_action_tdata,
    output wire        m_action_tvalid,
    input  wire        m_action_tready,
    output wire [31:0] mac_vision_acc_dbg,
    output wire [31:0] mac_xmod_acc_dbg,
    output wire [31:0] mac_gvsa_acc_dbg,
    output wire [31:0] mac_action_acc_dbg,

    // IP status telemetry
    output wire        vit_done,
    output wire        xmod_done,
    output wire        gvsa_done,
    output wire        adh_done
);
    wire ap_rst = ~rst_n;

    // ---- multi-rate scheduler (T0030) ----
    mr_vla_top u_scheduler (
        .clk(clk), .rst_n(rst_n),
        .s_vision_tdata(s_vision_tdata), .s_vision_tvalid(s_vision_tvalid), .s_vision_tready(s_vision_tready),
        .s_language_tdata(s_language_tdata), .s_language_tvalid(s_language_tvalid), .s_language_tready(s_language_tready),
        .m_action_tdata(m_action_tdata), .m_action_tvalid(m_action_tvalid), .m_action_tready(m_action_tready),
        .mac_vision_acc_dbg(mac_vision_acc_dbg), .mac_xmod_acc_dbg(mac_xmod_acc_dbg),
        .mac_gvsa_acc_dbg(mac_gvsa_acc_dbg), .mac_action_acc_dbg(mac_action_acc_dbg)
    );

    // ---- vit_encoder_12layer (T0029) ----
    vit_encoder_12layer u_vit_encoder (
        .ap_clk(clk), .ap_rst(ap_rst), .ap_start(vit_start), .ap_done(vit_done)
    );

    // ---- xmod_attn_v1 (T0024) ----
    xmod_attn_v1 u_xmod_attn (
        .ap_clk(clk), .ap_rst(ap_rst), .ap_start(xmod_start), .ap_done(xmod_done)
    );

    // ---- gvsa_matmul (T0025) ----
    gvsa_matmul u_gvsa_matmul (
        .ap_clk(clk), .ap_rst(ap_rst), .ap_start(gvsa_start), .ap_done(gvsa_done)
    );

    // ---- action_diff_head (T0026) ----
    action_diff_head u_action_diff (
        .ap_clk(clk), .ap_rst(ap_rst), .ap_start(adh_start), .ap_done(adh_done)
    );

endmodule

`default_nettype wire
