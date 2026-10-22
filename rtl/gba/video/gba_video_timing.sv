// SPDX-License-Identifier: GPL-3.0-or-later
// GBA LCD status clock, independent of HDMI and of stalled CPU requests.
// GBATEK LCD I/O: 1232 clocks/line, 228 lines/frame; HBlank flag at 1006.
// This does not render pixels or synchronize the HDMI framebuffer scanout.
module gba_video_timing (
    input logic clk, reset, ce,
    input logic access, we,
    input logic [9:0] addr,
    input logic [31:0] wdata,
    input logic [3:0] be,
    output logic selected,
    output logic [31:0] rdata,
    output logic [2:0] irq_events,
    output logic hblank_start, vblank_start,
    output logic [7:0] vcount,
    output logic [10:0] line_cycle
);
    logic [2:0] enables;
    logic [7:0] compare_line;
    logic [2:0] status, previous_status;
    always_comb begin
        status[0] = vcount >= 160 && vcount < 227;
        status[1] = line_cycle >= 1006;
        status[2] = vcount == compare_line;
    end
    assign selected = addr[9:2] == 1;
    assign rdata = {8'd0,vcount,compare_line,2'b00,enables,status};
    // Status edges last one system clock even when ce is held low. IF is sticky
    // in gba_timers_irq; enabling an already-active condition cannot retrigger it.
    assign irq_events = (status & ~previous_status) & enables & {3{!reset}};
    assign hblank_start = status[1] && !previous_status[1] && !reset;
    assign vblank_start = status[0] && !previous_status[0] && !reset;
    always_ff @(posedge clk) begin
        if (reset) begin
            vcount <= 0; line_cycle <= 0;
            enables <= 0; compare_line <= 0;
            previous_status <= 3'b100;
        end else begin
            previous_status <= status;
            if (ce) begin
                if (line_cycle == 1231) begin
                    line_cycle <= 0;
                    vcount <= vcount == 227 ? 8'd0 : vcount + 1'b1;
                end else line_cycle <= line_cycle + 1'b1;
            end
            if (access && selected && we) begin
                if (be[0]) enables <= wdata[5:3];
                if (be[1]) compare_line <= wdata[15:8];
            end
        end
    end
endmodule
