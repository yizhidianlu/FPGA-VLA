// mr_vla_sched_wrap.v — plain-Verilog wrapper around the SystemVerilog
// mr_vla_top scheduler, so Vivado IP-Integrator accepts it as a module
// reference (IPI requires the TOP file of a module reference to be Verilog
// or VHDL; mr_vla_top.sv is SystemVerilog and is kept as a supporting file).

`timescale 1ns/1ps

module mr_vla_sched_wrap (
    input  wire        clk,
    input  wire        rst_n,
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
    output wire [31:0] mac_action_acc_dbg
);
    mr_vla_top u_inner (
        .clk(clk), .rst_n(rst_n),
        .s_vision_tdata(s_vision_tdata), .s_vision_tvalid(s_vision_tvalid), .s_vision_tready(s_vision_tready),
        .s_language_tdata(s_language_tdata), .s_language_tvalid(s_language_tvalid), .s_language_tready(s_language_tready),
        .m_action_tdata(m_action_tdata), .m_action_tvalid(m_action_tvalid), .m_action_tready(m_action_tready),
        .mac_vision_acc_dbg(mac_vision_acc_dbg), .mac_xmod_acc_dbg(mac_xmod_acc_dbg),
        .mac_gvsa_acc_dbg(mac_gvsa_acc_dbg), .mac_action_acc_dbg(mac_action_acc_dbg)
    );
endmodule
