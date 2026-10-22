// SPDX-License-Identifier: GPL-3.0-or-later
module gba_audio_clock_test(
    input logic clk,reset,
    output logic audio_clk,wrap,
    output logic [19:0] cts,n_value
);
    logic [23:0] header;
    logic [55:0] sub[3:0];
    gba_hdmi_audio_clock clock_gen(.*);
    audio_clock_regeneration_packet #(.VIDEO_RATE(27.0E6),.AUDIO_RATE(48000)) acr(
        .clk_pixel(clk),.clk_audio(audio_clk),.clk_audio_counter_wrap(wrap),.header(header),.sub(sub));
    assign cts={sub[0][11:8],sub[0][23:16],sub[0][31:24]};
    assign n_value={sub[0][35:32],sub[0][47:40],sub[0][55:48]};
endmodule
