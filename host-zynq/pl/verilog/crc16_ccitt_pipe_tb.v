`timescale 1ns/1ps

module crc16_ccitt_pipe_tb;

reg clk;
reg rst_n;
reg [7:0] data_in;
reg valid;
reg sof;
reg eof;
reg [15:0] expected_crc;
reg check_en;
wire [15:0] crc_out;
wire crc_ok;
wire frame_done;

integer i;
reg [7:0] vec_pass [0:8];
reg [7:0] vec_fail [0:2];

crc16_ccitt_pipe dut (
    .clk(clk),
    .rst_n(rst_n),
    .data_in(data_in),
    .valid(valid),
    .sof(sof),
    .eof(eof),
    .expected_crc(expected_crc),
    .check_en(check_en),
    .crc_out(crc_out),
    .crc_ok(crc_ok),
    .frame_done(frame_done)
);

always #5 clk = ~clk;

task send_byte;
    input [7:0] b;
    input first;
    input last;
    begin
        @(posedge clk);
        data_in <= b;
        sof <= first;
        eof <= last;
        valid <= 1'b1;
        @(posedge clk);
        valid <= 1'b0;
        sof <= 1'b0;
        eof <= 1'b0;
    end
endtask

initial begin
    clk = 1'b0;
    rst_n = 1'b0;
    data_in = 8'd0;
    valid = 1'b0;
    sof = 1'b0;
    eof = 1'b0;
    expected_crc = 16'h0000;
    check_en = 1'b0;

    vec_pass[0] = "1";
    vec_pass[1] = "2";
    vec_pass[2] = "3";
    vec_pass[3] = "4";
    vec_pass[4] = "5";
    vec_pass[5] = "6";
    vec_pass[6] = "7";
    vec_pass[7] = "8";
    vec_pass[8] = "9";

    vec_fail[0] = "A";
    vec_fail[1] = "B";
    vec_fail[2] = "C";

    repeat (4) @(posedge clk);
    rst_n = 1'b1;

    /* Known vector: CRC16-CCITT-FALSE("123456789") = 0x29B1 */
    expected_crc = 16'h29B1;
    check_en = 1'b1;
    for (i = 0; i < 9; i = i + 1)
        send_byte(vec_pass[i], (i == 0), (i == 8));

    @(posedge clk);
    if (!frame_done)
        $fatal(1, "frame_done expected after pass frame");
    if (crc_out !== 16'h29B1)
        $fatal(1, "crc_out mismatch, expected 0x29B1 got 0x%0h", crc_out);
    if (!crc_ok)
        $fatal(1, "crc_ok expected high for pass frame");

    /* Fail path with intentionally wrong expected CRC */
    expected_crc = 16'h0000;
    check_en = 1'b1;
    for (i = 0; i < 3; i = i + 1)
        send_byte(vec_fail[i], (i == 0), (i == 2));

    @(posedge clk);
    if (!frame_done)
        $fatal(1, "frame_done expected after fail frame");
    if (crc_ok)
        $fatal(1, "crc_ok expected low for fail frame");

    $display("crc16_ccitt_pipe_tb passed");
    $finish;
end

endmodule
