open_project smoke_hls
set_top smoke_mac
add_files smoke_mac.cpp
add_files -tb smoke_mac_tb.cpp
open_solution sol1 -flow_target vivado
set_part xcvc1902-vsva2197-2MP-e-S
create_clock -period 4.0 -name default
csim_design
csynth_design
exit
