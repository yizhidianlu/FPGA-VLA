// rate_enable_gen.sv
// Generates 3 mutually independent single-cycle enable pulses on a 250 MHz clock:
//   vision_en   asserts once every  8,333,333 cycles  (~30 Hz)
//   language_en asserts once every  250,000,000 cycles (~1  Hz)
//   action_en   asserts once every  2,500,000 cycles  (~100 Hz)
// All three counters are independent free-running modulo-N counters. The pulse
// is asserted for exactly one clk period when the counter wraps. This is the
// honest "single PLL + sub-rate clock-enable" topology proposed for MR-VLA.

`default_nettype none

module rate_enable_gen #(
    parameter int VISION_CYCLES   = 8_333_333,
    parameter int LANGUAGE_CYCLES = 250_000_000,
    parameter int ACTION_CYCLES   = 2_500_000
) (
    input  wire logic clk,
    input  wire logic rst_n,
    output wire logic vision_en,
    output wire logic language_en,
    output wire logic action_en
);
    // Width chosen to hold (CYCLES-1).
    logic [22:0] cnt_v;   // ceil(log2(8_333_333))   = 23
    logic [27:0] cnt_l;   // ceil(log2(250_000_000)) = 28
    logic [21:0] cnt_a;   // ceil(log2(2_500_000))   = 22

    logic v_pulse, l_pulse, a_pulse;

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            cnt_v <= '0;
            cnt_l <= '0;
            cnt_a <= '0;
            v_pulse <= 1'b0;
            l_pulse <= 1'b0;
            a_pulse <= 1'b0;
        end else begin
            // VISION
            if (cnt_v == VISION_CYCLES - 1) begin
                cnt_v   <= '0;
                v_pulse <= 1'b1;
            end else begin
                cnt_v   <= cnt_v + 1'b1;
                v_pulse <= 1'b0;
            end
            // LANGUAGE
            if (cnt_l == LANGUAGE_CYCLES - 1) begin
                cnt_l   <= '0;
                l_pulse <= 1'b1;
            end else begin
                cnt_l   <= cnt_l + 1'b1;
                l_pulse <= 1'b0;
            end
            // ACTION
            if (cnt_a == ACTION_CYCLES - 1) begin
                cnt_a   <= '0;
                a_pulse <= 1'b1;
            end else begin
                cnt_a   <= cnt_a + 1'b1;
                a_pulse <= 1'b0;
            end
        end
    end

    assign vision_en   = v_pulse;
    assign language_en = l_pulse;
    assign action_en   = a_pulse;
endmodule

`default_nettype wire
