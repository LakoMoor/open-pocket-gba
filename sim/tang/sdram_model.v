// The GW2AR-18 embedded SDRAM (64 Mbit, 2M x 32: 4 banks, 2048 rows, 256 columns).
// Commands are sampled on the rising edge of the chip clock; the CAS latency comes from
// the mode register; read data is on DQ from edge CL-1 to edge CL after the command, so it
// is valid at the CL-th edge like the real part (output delay tAC within the first cycle).
`timescale 1ps/1ps
module sdram_model (
    input             clk, cke, cs_n, ras_n, cas_n, we_n,
    input       [3:0] dqm,
    input      [10:0] addr,
    input       [1:0] ba,
    inout      [31:0] dq
);
    reg [31:0] mem [0:2097151];
    reg [10:0] row [0:3];
    reg  [2:0] cl = 3'd2;
    reg [31:0] pdata [0:3];
    reg  [3:0] pvalid = 0;
    reg [31:0] dq_o;
    reg        dq_oe = 0;
    assign dq = dq_oe ? dq_o : 32'bz;

    wire [20:0] idx = {ba, row[ba], addr[7:0]};
    integer i;
    integer dbg_n = 0;
    reg dbg = 0;
    initial dbg = $test$plusargs("sdram_debug");
    always @(posedge clk) if (dbg && cke && !cs_n && {ras_n, cas_n, we_n} == 3'b101 && ba == 2'd3 && row[ba] >= 11'h600 && dbg_n < 400) begin
        dbg_n = dbg_n + 1;
        $display("sdram %0t READ ba %0d row %h col %h -> %h", $time, ba, row[ba], addr[7:0], mem[idx]);
    end
    always @(negedge clk) if (dbg && dq_oe && dbg_n > 1000) $display("        %0t dq out %h", $time, dq_o);
    always @(posedge clk) begin
        dq_oe <= pvalid[0];
        dq_o  <= pdata[0];
        for (i = 0; i < 3; i = i + 1) begin pdata[i] <= pdata[i + 1]; pvalid[i] <= pvalid[i + 1]; end
        pvalid[3] <= 0;
        if (cke && !cs_n) case ({ras_n, cas_n, we_n})
            3'b011: row[ba] <= addr;                                     // ACTIVE
            3'b101: begin pdata[cl - 3'd2] <= mem[idx]; pvalid[cl - 3'd2] <= 1; end   // READ: on DQ from the next edge (tAC) to the CL-th
            3'b100: begin                                                // WRITE
                if (!dqm[0]) mem[idx][7:0]   <= dq[7:0];
                if (!dqm[1]) mem[idx][15:8]  <= dq[15:8];
                if (!dqm[2]) mem[idx][23:16] <= dq[23:16];
                if (!dqm[3]) mem[idx][31:24] <= dq[31:24];
            end
            3'b000: cl <= addr[6:4];                                     // LOAD MODE
            default: ;
        endcase
    end
endmodule
