# OOC constraints for T0036_v5 — provides a clock + I/O delays at the BD
# wrapper boundary so post-route timing analysis has a valid scope.

# 250 MHz primary clock (matches T0030 multi-rate scheduler base).
create_clock -name clk -period 4.0 [get_ports clk]

# Generic input/output delays so set_max_delay / set_min_delay engines
# fire on the externally-bound nets even though OOC does not place IOBs.
set_input_delay  -clock clk -max 1.5 [all_inputs]
set_input_delay  -clock clk -min 0.2 [all_inputs]
set_output_delay -clock clk -max 1.5 [all_outputs]
set_output_delay -clock clk -min 0.2 [all_outputs]
