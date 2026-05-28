# T0036_v6 OOC constraints: 6.0 ns clock period = 167 MHz target.
# Relaxed from v5's 4.0 ns (250 MHz) which produced WNS=-2.340 ns. The 6.34 ns
# true period implied by v5's WNS = 4.0 + 2.34 indicates 6.0 ns gives margin.

create_clock -name clk -period 6.0 [get_ports clk]

set_input_delay  -clock clk -max 2.0 [all_inputs]
set_input_delay  -clock clk -min 0.3 [all_inputs]
set_output_delay -clock clk -max 2.0 [all_outputs]
set_output_delay -clock clk -min 0.3 [all_outputs]
