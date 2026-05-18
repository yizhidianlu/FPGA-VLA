# tri_rate_toy.xdc
# Single 250 MHz primary clock. Asynchronous active-low reset.
# Note: no physical pin assignments (synth-only flow, not placement/route).

create_clock -period 4.000 -name clk [get_ports clk]

# rst_n is treated as async input to the design — false-path it from clk timing
set_false_path -from [get_ports rst_n]

# Input/output delay budgets (conservative; for synth-only this is informational)
set_input_delay  -clock clk -max 1.0 [get_ports {s_vision_tdata[*] s_vision_tvalid s_language_tdata[*] s_language_tvalid m_action_tready}]
set_input_delay  -clock clk -min 0.2 [get_ports {s_vision_tdata[*] s_vision_tvalid s_language_tdata[*] s_language_tvalid m_action_tready}]
set_output_delay -clock clk -max 1.0 [get_ports {m_action_tdata[*] m_action_tvalid s_vision_tready s_language_tready mac_*_acc_dbg[*]}]
set_output_delay -clock clk -min 0.2 [get_ports {m_action_tdata[*] m_action_tvalid s_vision_tready s_language_tready mac_*_acc_dbg[*]}]
