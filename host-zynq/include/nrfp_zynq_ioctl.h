#ifndef NRFP_ZYNQ_IOCTL_H
#define NRFP_ZYNQ_IOCTL_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint64_t __u64;
#endif

#include "../../common/include/nrfp_proto.h"

#define NRFP_ZYNQ_DEVICE_NAME "nrfp-zynq0"
#define NRFP_ZYNQ_IOC_MAGIC 0xA4

struct nrfp_zynq_frame_req {
__u8 channel;
__u8 opcode;
__u8 status;
__u8 flags;
__u16 payload_len;
__u8 payload[NRFP_TL_MAX_PAYLOAD];
};

struct nrfp_zynq_event_msg {
__u8 channel;
__u8 opcode;
__u8 status;
__u8 flags;
__u8 seq;
__u8 payload_len;
__u8 payload[96];
__u64 timestamp_ns;
};

struct nrfp_zynq_stats {
__u64 tx_frames;
__u64 tx_bytes;
__u64 rx_events;
__u64 irq_count;
__u64 rx_dropped;
/* PL-related counters (populated when PL registers are mapped) */
__u64 rx_frames;       /* TL frames successfully received and CRC-checked */
__u64 crc_errors;      /* frames rejected due to CRC mismatch (PL hw or software) */
__u64 pl_rx_overflow;  /* RX FIFO overflow events accumulated from PL COUNTER reg */
__u64 pl_tx_overflow;  /* TX FIFO overflow events accumulated from PL COUNTER reg */
};

/*
 * Runtime PL configuration: watermark thresholds and CRC offload enable.
 * Applied via NRFP_ZYNQ_IOC_PL_CFG; takes effect immediately.
 */
struct nrfp_zynq_pl_cfg {
__u8 use_pl_crc; /* 1 = use PL hardware CRC result, 0 = software fallback */
__u8 rx_wm;      /* RX FIFO watermark in bytes (0 = keep current) */
__u8 tx_wm;      /* TX FIFO watermark in bytes (0 = keep current) */
__u8 reserved;
};

#define NRFP_ZYNQ_IOC_SEND_FRAME _IOW(NRFP_ZYNQ_IOC_MAGIC, 0x01, struct nrfp_zynq_frame_req)
#define NRFP_ZYNQ_IOC_GET_STATS  _IOR(NRFP_ZYNQ_IOC_MAGIC, 0x02, struct nrfp_zynq_stats)
#define NRFP_ZYNQ_IOC_PL_CFG    _IOW(NRFP_ZYNQ_IOC_MAGIC, 0x03, struct nrfp_zynq_pl_cfg)

#endif /* NRFP_ZYNQ_IOCTL_H */
