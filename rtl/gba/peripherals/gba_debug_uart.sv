// SPDX-License-Identifier: GPL-3.0-or-later
// Periodic, read-only bring-up telemetry: 32 hex digits (128-bit snapshot), LF.
// Never participates in CPU/memory handshakes.
module gba_debug_uart #(
    parameter integer CLK_HZ=33750000,
    parameter integer BAUD=115200,
    parameter integer INTERVAL=CLK_HZ/4
) (
    input logic clk,reset,
    input logic [127:0] snapshot,
    output logic tx
);
    localparam integer DIV=CLK_HZ/BAUD;
    logic [25:0] interval_count;
    logic [15:0] bit_count;
    logic [3:0] bits_left;
    logic [5:0] character;
    logic active;
    logic [127:0] saved;
    logic [9:0] shift;
    wire [3:0] nibble=saved[127:124];
    wire [7:0] ascii=nibble<10 ? 8'h30+{4'd0,nibble} : 8'h61+{4'd0,nibble}-8'd10;
    assign tx=shift[0];
    always_ff @(posedge clk) begin
        if(reset) begin
            interval_count<=0;bit_count<=0;bits_left<=0;character<=0;
            active<=0;saved<=0;shift<=10'h3ff;
        end else if(bits_left!=0) begin
            if(bit_count==0) begin
                shift<={1'b1,shift[9:1]};bits_left<=bits_left-1'b1;bit_count<=16'(DIV-1);
            end else bit_count<=bit_count-1'b1;
        end else if(active) begin
            shift<={1'b1,character==32 ? 8'h0a : ascii,1'b0};
            bits_left<=10;bit_count<=16'(DIV-1);
            if(character==32) begin active<=0;interval_count<=0; end
            else begin character<=character+1'b1;saved<={saved[123:0],4'd0};end
        end else if(interval_count==INTERVAL-1) begin
            saved<=snapshot;character<=0;active<=1;
        end else interval_count<=interval_count+1'b1;
    end
endmodule
