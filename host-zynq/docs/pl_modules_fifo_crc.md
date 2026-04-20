# Zynq PL 模块落地说明：`spi_rx_tx_fifo` + `crc16_ccitt_pipe`

本文档对应 `host-zynq/pl/verilog/spi_rx_tx_fifo.v` 与 `host-zynq/pl/verilog/crc16_ccitt_pipe.v`，用于说明其在 PS/Linux 驱动路径中的接入方式、寄存器建议和联调方法。

## 1) 架构与数据路径

```mermaid
flowchart LR
    NRF[nRF5340 SPI Slave]
    SPI[(PS SPI Controller)]
    FIFO[spi_rx_tx_fifo]
    CRC[crc16_ccitt_pipe]
    PS[Linux driver\n(nrfp_zynq_host.c)]

    NRF <-- SPI stream --> FIFO
    FIFO -->|RX stream| CRC
    CRC -->|crc_out/crc_ok/frame_done| PS
    FIFO -->|RX/TX level, watermark IRQ| PS
    PS -->|TX stream| FIFO
    PS <-->|AXI-Lite-like regs| FIFO
```

> 说明：`spi_rx_tx_fifo` 提供双向缓冲与水位中断；`crc16_ccitt_pipe` 提供流式 CRC16-CCITT-FALSE 计算/校验。

## 2) 信号列表与时序要点

## 2.1 `spi_rx_tx_fifo` 关键信号

- RX ingress（SPI 侧输入）
  - `s_rx_data[7:0]`
  - `s_rx_valid`
  - `s_rx_last`
  - `s_rx_ready`
- RX egress（PS 侧输出）
  - `m_rx_data[7:0]`
  - `m_rx_valid`
  - `m_rx_last`
  - `m_rx_ready`
- TX ingress（PS 侧输入）
  - `s_tx_data[7:0]`
  - `s_tx_valid`
  - `s_tx_last`
  - `s_tx_ready`
- TX egress（SPI 侧输出）
  - `m_tx_data[7:0]`
  - `m_tx_valid`
  - `m_tx_last`
  - `m_tx_ready`
- 状态与中断
  - `rx_level/tx_level`
  - `rx_wm_hit/tx_wm_hit`
  - `irq`
- 简化寄存器接口（扩展点）
  - `reg_wr_en/reg_rd_en`
  - `reg_addr[3:0]`
  - `reg_wdata/reg_rdata`
  - `reg_ack`

时序注意：
- 所有 stream 口遵循 `valid & ready` 握手。
- `*_last` 与对应字节同周期采样。
- `rx_wm_hit/tx_wm_hit` 为电平判定；`sticky_*` 用于中断锁存，需软件清除。

## 2.2 `crc16_ccitt_pipe` 关键信号

- 输入
  - `data_in[7:0]`
  - `valid`
  - `sof`（首字节）
  - `eof`（末字节）
  - `expected_crc[15:0]`
  - `check_en`
- 输出
  - `crc_out[15:0]`
  - `crc_ok`
  - `frame_done`

CRC 语义：
- Poly = `0x1021`
- Init = `0xFFFF`
- XorOut = `0x0000`
- RefIn/RefOut = false

时序注意：
- `sof` 与首字节同拍。
- `eof` 与末字节同拍。
- `frame_done` 在帧末字节后一个时钟周期内拉高（单拍脉冲）。

## 3) FIFO/CRC 统计寄存器建议（提案）

面向 `spi_rx_tx_fifo` 的最小寄存器映射：

| 偏移 | 名称 | R/W | 字段 | 含义 |
|---|---|---|---|---|
| 0x00 | CTRL | R/W | `bit0 irq_en_rx_wm`, `bit1 irq_en_tx_wm`, `bit8 clr_rx_sticky`, `bit9 clr_tx_sticky`, `bit10 clr_counters` | 控制与清除 |
| 0x04 | WM_CFG | R/W | `[ADDR_WIDTH:0] rx_wm`, `[16 +: ADDR_WIDTH+1] tx_wm` | 水位阈值 |
| 0x08 | STATUS | R | `rx_level`, `tx_level` | 当前 FIFO 深度 |
| 0x0C | COUNTER | R | `[15:0] rx_overflow_cnt`, `[31:16] tx_overflow_cnt` | 溢出计数 |

CRC 侧（建议由上层寄存器桥接）：

| 偏移 | 名称 | R/W | 字段 | 含义 |
|---|---|---|---|---|
| 0x20 | CRC_LAST | R | `crc_out` | 最近帧 CRC 结果 |
| 0x24 | CRC_STAT | R | `frame_done`, `crc_ok` | 最近帧校验状态 |
| 0x28 | CRC_ERR_CNT | R | `crc_fail_count` | 累积 CRC 失败计数（可选） |

## 4) PS/Linux 驱动接入建议与软件兜底

结合 `host-zynq/kernel/nrfp_zynq_host.c` 的最小接入流程：

1. IRQ 触发后先读 FIFO `STATUS`：
   - 若 `rx_level >= rx_wm` 或 `rx_level > 0`，批量读取 RX stream。
2. 每帧结束时读取/采样 CRC 结果：
   - `frame_done=1` 时消费 `crc_out/crc_ok`。
3. `crc_ok=0` 时：
   - 增加内核统计并触发现有协议 NACK/重传路径。
4. 软件 fallback：
   - 保留 `common/include/nrfp_proto.h` 的软件 CRC 校验逻辑，PL CRC 可通过驱动开关旁路。
   - 当 PL CRC 不可用、时序未稳定或调试阶段，直接走软件 CRC，协议行为保持一致。

## 5) bring-up checklist

- [ ] Vivado 中确认 `spi_rx_tx_fifo`/`crc16_ccitt_pipe` 已接入同一时钟复位域。
- [ ] `WM_CFG` 设置为保守值（如 rx=4, tx=2）避免早期抖动。
- [ ] `CTRL` 开启 watermark IRQ。
- [ ] Linux 侧能读到 `STATUS`/`COUNTER` 并观察随流量变化。
- [ ] 发送已知向量（`123456789`）确认 CRC=`0x29B1`。
- [ ] 注入错误 CRC，确认 `crc_ok=0` 且软件重传路径生效。

## 6) debug tips

- RX 丢包：优先检查 `rx_overflow_cnt`，再降低 watermark 或增加 FIFO 深度。
- TX 卡顿：检查 `tx_level` 是否长期满，确认 SPI 出口 `m_tx_ready` 及时返回。
- CRC 全部失败：先核对 Init/Poly/XorOut 是否与协议一致，再核对 `sof/eof` 对齐。
- 中断风暴：关闭 `irq_en_*` 验证电平来源，确保 sticky 清除流程正确。

## 7) 自检 testbench

- FIFO 模块：`host-zynq/pl/verilog/spi_rx_tx_fifo_tb.v`
  - 覆盖点：RX/TX burst、`last` 传播、水位命中、sticky/IRQ 清除。
- CRC 模块：`host-zynq/pl/verilog/crc16_ccitt_pipe_tb.v`
  - 覆盖点：已知向量通过、错误 CRC 失败路径、`frame_done` 行为。
