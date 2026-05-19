// bram_axis_fifo.sv
// Synchronous AXI-Stream FIFO with REGISTERED READ. For DEPTH >= ~512, Vivado
// will infer BRAM (RAMB36/RAMB18). For smaller DEPTH, distributed-RAM is used.
//
// Sync read adds 1-cycle read latency vs the async-read pattern from T0021.
// That's necessary so the read port can map to BRAM (BRAMs do not support
// asynchronous read).

`default_nettype none

module bram_axis_fifo #(
    parameter int WIDTH = 32,
    parameter int DEPTH = 1024   // power of two preferred for cheap modulo
) (
    input  wire logic              clk,
    input  wire logic              rst_n,
    // slave (write) side
    input  wire logic [WIDTH-1:0]  s_axis_tdata,
    input  wire logic              s_axis_tvalid,
    output      logic              s_axis_tready,
    // master (read) side
    output      logic [WIDTH-1:0]  m_axis_tdata,
    output      logic              m_axis_tvalid,
    input  wire logic              m_axis_tready
);
    localparam int AW = $clog2(DEPTH);

    (* ram_style = "block" *) logic [WIDTH-1:0] mem [0:DEPTH-1];

    logic [AW:0] wptr, rptr;
    logic        empty, full;

    assign empty = (wptr == rptr);
    assign full  = (wptr[AW] != rptr[AW]) && (wptr[AW-1:0] == rptr[AW-1:0]);

    assign s_axis_tready = !full;

    // BRAM sync read: m_axis_tdata is registered.
    logic              read_valid_q;
    logic [WIDTH-1:0]  read_data_q;
    logic              do_read;

    assign do_read     = !empty && (!read_valid_q || m_axis_tready);
    assign m_axis_tdata  = read_data_q;
    assign m_axis_tvalid = read_valid_q;

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            wptr <= '0;
            rptr <= '0;
            read_valid_q <= 1'b0;
        end else begin
            // write
            if (s_axis_tvalid && s_axis_tready) begin
                mem[wptr[AW-1:0]] <= s_axis_tdata;
                wptr <= wptr + 1'b1;
            end
            // read
            if (do_read) begin
                read_data_q  <= mem[rptr[AW-1:0]];
                read_valid_q <= 1'b1;
                rptr         <= rptr + 1'b1;
            end else if (read_valid_q && m_axis_tready) begin
                read_valid_q <= 1'b0;
            end
        end
    end
endmodule

`default_nettype wire
