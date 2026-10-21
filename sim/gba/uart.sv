// SPDX-License-Identifier: GPL-3.0-or-later
module gba_uart_test(input logic clk,reset,input logic [127:0] snapshot,output logic tx);
    gba_debug_uart #(.CLK_HZ(460800),.BAUD(115200),.INTERVAL(40)) uart(.*);
endmodule
