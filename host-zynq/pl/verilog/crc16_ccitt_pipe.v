/*
 * Streaming CRC16-CCITT-FALSE pipeline.
 *
 * Polynomial : 0x1021
 * Init value : 0xFFFF
 * XorOut     : 0x0000
 * RefIn/Out  : false
 */
module crc16_ccitt_pipe (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [7:0]  data_in,
    input  wire        valid,
    input  wire        sof,
    input  wire        eof,
    input  wire [15:0] expected_crc,
    input  wire        check_en,
    output reg  [15:0] crc_out,
    output reg         crc_ok,
    output reg         frame_done
);

function [15:0] crc16_next;
    input [15:0] crc;
    input [7:0]  data;
    integer i;
    reg [15:0] c;
begin
    c = crc ^ {data, 8'd0};
    for (i = 0; i < 8; i = i + 1) begin
        if (c[15])
            c = (c << 1) ^ 16'h1021;
        else
            c = (c << 1);
    end
    crc16_next = c;
end
endfunction

reg [15:0] crc_state;
reg        in_frame;
wire [15:0] crc_seed;
wire [15:0] crc_next_byte;

assign crc_seed = (sof || !in_frame) ? 16'hFFFF : crc_state;
assign crc_next_byte = crc16_next(crc_seed, data_in);

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        crc_state <= 16'hFFFF;
        crc_out <= 16'h0000;
        crc_ok <= 1'b0;
        frame_done <= 1'b0;
        in_frame <= 1'b0;
    end else begin
        frame_done <= 1'b0;

        if (valid) begin
            if (sof)
                in_frame <= 1'b1;

            if (eof) begin
                frame_done <= 1'b1;
                crc_out <= crc_next_byte;
                crc_ok <= check_en && (crc_next_byte == expected_crc);
                in_frame <= 1'b0;
                crc_state <= 16'hFFFF;
            end else begin
                crc_state <= crc_next_byte;
            end
        end
    end
end

endmodule
