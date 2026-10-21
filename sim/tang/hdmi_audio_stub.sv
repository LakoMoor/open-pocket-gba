// HDMI audio clock regeneration packet: not simulated (real-valued parameters that Verilator
// cannot fold); the packet stays empty, the video timing of the hdmi module is unaffected.
module audio_clock_regeneration_packet #(
    parameter real VIDEO_RATE = 25.2E6,
    parameter int AUDIO_RATE = 48e3
) (
    input  logic clk_pixel,
    input  logic clk_audio,
    output logic clk_audio_counter_wrap,
    output logic [23:0] header,
    output logic [55:0] sub [3:0]
);
    assign clk_audio_counter_wrap = 1'b0;
    assign header = 24'd0;
    assign sub[0] = 56'd0; assign sub[1] = 56'd0; assign sub[2] = 56'd0; assign sub[3] = 56'd0;
endmodule
