// SPDX-License-Identifier: GPL-3.0-or-later
// 27 MHz pixel clock -> average 48 kHz audio clock, 281/282 pixel half periods.
// 96000 / 27000000 = 4 / 1125. Keeps HDMI ACR alive even with silent samples.
module gba_hdmi_audio_clock(input logic clk,reset,output logic audio_clk);
    logic [10:0] phase;
    wire [11:0] next_phase={1'b0,phase}+12'd4;
    always_ff @(posedge clk) begin
        if(reset) begin phase<=0;audio_clk<=0;end
        else if(next_phase>=1125) begin phase<=11'(next_phase-12'd1125);audio_clk<=!audio_clk;end
        else phase<=next_phase[10:0];
    end
endmodule
