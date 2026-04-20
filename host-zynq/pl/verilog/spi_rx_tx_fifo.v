/*
 * SPI RX/TX FIFO bridge for Zynq PS<->PL integration.
 *
 * - RX path: SPI-side stream (s_rx_*) buffered then forwarded to PS-side stream (m_rx_*).
 * - TX path: PS-side stream (s_tx_*) buffered then forwarded to SPI-side stream (m_tx_*).
 * - Watermark/status outputs are available for PS polling or interrupt generation.
 * - Simplified register interface provides control/status and extension points.
 */
module spi_rx_tx_fifo #(
    parameter integer DATA_WIDTH = 8,
    parameter integer DEPTH = 16,
    parameter integer ADDR_WIDTH = 4
) (
    input  wire                     clk,
    input  wire                     rst_n,

    /* SPI -> RX FIFO ingress */
    input  wire [DATA_WIDTH-1:0]    s_rx_data,
    input  wire                     s_rx_valid,
    input  wire                     s_rx_last,
    output wire                     s_rx_ready,

    /* RX FIFO -> PS stream egress */
    output wire [DATA_WIDTH-1:0]    m_rx_data,
    output wire                     m_rx_valid,
    output wire                     m_rx_last,
    input  wire                     m_rx_ready,

    /* PS -> TX FIFO ingress */
    input  wire [DATA_WIDTH-1:0]    s_tx_data,
    input  wire                     s_tx_valid,
    input  wire                     s_tx_last,
    output wire                     s_tx_ready,

    /* TX FIFO -> SPI stream egress */
    output wire [DATA_WIDTH-1:0]    m_tx_data,
    output wire                     m_tx_valid,
    output wire                     m_tx_last,
    input  wire                     m_tx_ready,

    /* Simplified register-facing interface (AXI-lite-like extension point) */
    input  wire                     reg_wr_en,
    input  wire                     reg_rd_en,
    input  wire [3:0]               reg_addr,
    input  wire [31:0]              reg_wdata,
    output reg  [31:0]              reg_rdata,
    output reg                      reg_ack,

    /* Status outputs for polling/interrupt */
    output wire [ADDR_WIDTH:0]      rx_level,
    output wire [ADDR_WIDTH:0]      tx_level,
    output wire                     rx_wm_hit,
    output wire                     tx_wm_hit,
    output wire                     irq
);

localparam [3:0] REG_CTRL    = 4'h0;
localparam [3:0] REG_WM_CFG  = 4'h1;
localparam [3:0] REG_STATUS  = 4'h2;
localparam [3:0] REG_COUNTER = 4'h3;
localparam [ADDR_WIDTH:0] DEPTH_VAL = DEPTH;

reg [DATA_WIDTH-1:0] rx_mem [0:DEPTH-1];
reg                  rx_last_mem [0:DEPTH-1];
reg [ADDR_WIDTH-1:0] rx_wr_ptr;
reg [ADDR_WIDTH-1:0] rx_rd_ptr;
reg [ADDR_WIDTH:0]   rx_count;

reg [DATA_WIDTH-1:0] tx_mem [0:DEPTH-1];
reg                  tx_last_mem [0:DEPTH-1];
reg [ADDR_WIDTH-1:0] tx_wr_ptr;
reg [ADDR_WIDTH-1:0] tx_rd_ptr;
reg [ADDR_WIDTH:0]   tx_count;

reg [ADDR_WIDTH:0] cfg_rx_wm;
reg [ADDR_WIDTH:0] cfg_tx_wm;
reg                irq_en_rx_wm;
reg                irq_en_tx_wm;
reg                sticky_rx_wm;
reg                sticky_tx_wm;
reg [15:0]         rx_overflow_cnt;
reg [15:0]         tx_overflow_cnt;

wire rx_full;
wire rx_empty;
wire tx_full;
wire tx_empty;
wire rx_push;
wire rx_pop;
wire tx_push;
wire tx_pop;

assign rx_full = (rx_count == DEPTH_VAL);
assign rx_empty = (rx_count == { (ADDR_WIDTH+1){1'b0} });
assign tx_full = (tx_count == DEPTH_VAL);
assign tx_empty = (tx_count == { (ADDR_WIDTH+1){1'b0} });

assign s_rx_ready = ~rx_full;
assign m_rx_valid = ~rx_empty;
assign m_rx_data = rx_mem[rx_rd_ptr];
assign m_rx_last = rx_last_mem[rx_rd_ptr];

assign s_tx_ready = ~tx_full;
assign m_tx_valid = ~tx_empty;
assign m_tx_data = tx_mem[tx_rd_ptr];
assign m_tx_last = tx_last_mem[tx_rd_ptr];

assign rx_push = s_rx_valid & s_rx_ready;
assign rx_pop = m_rx_valid & m_rx_ready;
assign tx_push = s_tx_valid & s_tx_ready;
assign tx_pop = m_tx_valid & m_tx_ready;

assign rx_level = rx_count;
assign tx_level = tx_count;
assign rx_wm_hit = (rx_count >= cfg_rx_wm) & (cfg_rx_wm != { (ADDR_WIDTH+1){1'b0} });
assign tx_wm_hit = (tx_count >= cfg_tx_wm) & (cfg_tx_wm != { (ADDR_WIDTH+1){1'b0} });
assign irq = (irq_en_rx_wm & sticky_rx_wm) | (irq_en_tx_wm & sticky_tx_wm);

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        rx_wr_ptr <= {ADDR_WIDTH{1'b0}};
        rx_rd_ptr <= {ADDR_WIDTH{1'b0}};
        rx_count <= {(ADDR_WIDTH+1){1'b0}};
        tx_wr_ptr <= {ADDR_WIDTH{1'b0}};
        tx_rd_ptr <= {ADDR_WIDTH{1'b0}};
        tx_count <= {(ADDR_WIDTH+1){1'b0}};
        cfg_rx_wm <= {{ADDR_WIDTH{1'b0}}, 1'b1};
        cfg_tx_wm <= {{ADDR_WIDTH{1'b0}}, 1'b1};
        irq_en_rx_wm <= 1'b0;
        irq_en_tx_wm <= 1'b0;
        sticky_rx_wm <= 1'b0;
        sticky_tx_wm <= 1'b0;
        rx_overflow_cnt <= 16'd0;
        tx_overflow_cnt <= 16'd0;
        reg_rdata <= 32'd0;
        reg_ack <= 1'b0;
    end else begin
        reg_ack <= reg_wr_en | reg_rd_en;

        /* RX FIFO write/read */
        if (rx_push) begin
            rx_mem[rx_wr_ptr] <= s_rx_data;
            rx_last_mem[rx_wr_ptr] <= s_rx_last;
            rx_wr_ptr <= rx_wr_ptr + {{(ADDR_WIDTH-1){1'b0}}, 1'b1};
        end
        if (s_rx_valid & ~s_rx_ready) begin
            rx_overflow_cnt <= rx_overflow_cnt + 16'd1;
        end

        if (rx_pop)
            rx_rd_ptr <= rx_rd_ptr + {{(ADDR_WIDTH-1){1'b0}}, 1'b1};

        case ({rx_push, rx_pop})
            2'b10: rx_count <= rx_count + {{ADDR_WIDTH{1'b0}}, 1'b1};
            2'b01: rx_count <= rx_count - {{ADDR_WIDTH{1'b0}}, 1'b1};
            default: ;
        endcase

        /* TX FIFO write/read */
        if (tx_push) begin
            tx_mem[tx_wr_ptr] <= s_tx_data;
            tx_last_mem[tx_wr_ptr] <= s_tx_last;
            tx_wr_ptr <= tx_wr_ptr + {{(ADDR_WIDTH-1){1'b0}}, 1'b1};
        end
        if (s_tx_valid & ~s_tx_ready) begin
            tx_overflow_cnt <= tx_overflow_cnt + 16'd1;
        end

        if (tx_pop)
            tx_rd_ptr <= tx_rd_ptr + {{(ADDR_WIDTH-1){1'b0}}, 1'b1};

        case ({tx_push, tx_pop})
            2'b10: tx_count <= tx_count + {{ADDR_WIDTH{1'b0}}, 1'b1};
            2'b01: tx_count <= tx_count - {{ADDR_WIDTH{1'b0}}, 1'b1};
            default: ;
        endcase

        if (rx_wm_hit)
            sticky_rx_wm <= 1'b1;
        if (tx_wm_hit)
            sticky_tx_wm <= 1'b1;

        if (reg_wr_en) begin
            case (reg_addr)
                REG_CTRL: begin
                    irq_en_rx_wm <= reg_wdata[0];
                    irq_en_tx_wm <= reg_wdata[1];
                    if (reg_wdata[8])
                        sticky_rx_wm <= 1'b0;
                    if (reg_wdata[9])
                        sticky_tx_wm <= 1'b0;
                    if (reg_wdata[10]) begin
                        rx_overflow_cnt <= 16'd0;
                        tx_overflow_cnt <= 16'd0;
                    end
                end
                REG_WM_CFG: begin
                    cfg_rx_wm <= reg_wdata[ADDR_WIDTH:0];
                    cfg_tx_wm <= reg_wdata[16 +: (ADDR_WIDTH+1)];
                end
                default: ;
            endcase
        end

        if (reg_rd_en) begin
            case (reg_addr)
                REG_CTRL: begin
                    reg_rdata <= {
                        20'd0,
                        1'b0,
                        1'b0,
                        sticky_tx_wm,
                        sticky_rx_wm,
                        4'd0,
                        irq_en_tx_wm,
                        irq_en_rx_wm
                    };
                end
                REG_WM_CFG: begin
                    reg_rdata <= {
                        {(16-(ADDR_WIDTH+1)){1'b0}}, cfg_tx_wm,
                        {(16-(ADDR_WIDTH+1)){1'b0}}, cfg_rx_wm
                    };
                end
                REG_STATUS: begin
                    reg_rdata <= {
                        {(16-(ADDR_WIDTH+1)){1'b0}}, tx_count,
                        {(16-(ADDR_WIDTH+1)){1'b0}}, rx_count
                    };
                end
                REG_COUNTER: begin
                    reg_rdata <= {tx_overflow_cnt, rx_overflow_cnt};
                end
                default: reg_rdata <= 32'd0;
            endcase
        end
    end
end

endmodule
