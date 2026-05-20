open_project vit_enc_hls -reset
set_top vit_encoder_12layer
add_files vit_encoder_12layer.cpp   -cflags "-std=c++14 -I."
add_files vit_transformer_layer.cpp -cflags "-std=c++14 -I."
add_files -tb vit_encoder_12layer_tb.cpp -cflags "-std=c++14 -I."
open_solution sol1 -flow_target vivado
set_part xcvc1902-vsva2197-2MP-e-S
create_clock -period 4.0 -name default
csim_design
csynth_design
exit
