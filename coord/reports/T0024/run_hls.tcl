open_project xmod_attn_v1_hls -reset
set_top xmod_attn_v1
add_files xmod_attn_v1.cpp      -cflags "-std=c++14 -I."
add_files q_kt_matmul_v1.cpp    -cflags "-std=c++14 -I."
add_files softmax_64_v1.cpp     -cflags "-std=c++14 -I."
add_files weighted_sum_v1.cpp   -cflags "-std=c++14 -I."
add_files topk_engine.cpp       -cflags "-std=c++14 -I."
add_files -tb xmod_attn_v1_tb.cpp -cflags "-std=c++14 -I."
open_solution sol1 -flow_target vivado
set_part xcvc1902-vsva2197-2MP-e-S
create_clock -period 4.0 -name default
csim_design
csynth_design
exit
