#ifndef NRFP_TRANSPORT_H
#define NRFP_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nrfp_proto.h"

#define NRFP_FLAG_REQ_ACK NRFP_FLAG_ACK_REQ
#define NRFP_FLAG_IS_NAK 0x80u

#define NRFP_RETRY_TIMEOUT_MS 50u
#define NRFP_RETRY_MAX_ATTEMPTS 3u
#define NRFP_CRC_ERROR_RESET_THRESHOLD 8u
#define NRFP_TX_QUEUE_DEPTH 16u
#define NRFP_FRAGMENT_META_SIZE 4u
#define NRFP_FRAME_WIRE_MAX (sizeof(struct nrfp_tl_header) + NRFP_TL_MAX_PAYLOAD + sizeof(uint16_t))

struct nrfp_frame {
struct nrfp_tl_header hdr;
const uint8_t *payload;
};

struct nrfp_fragment_cursor {
uint16_t offset;
uint8_t index;
uint8_t total;
};

struct nrfp_rx_health {
uint8_t crc_error_streak;
};

struct nrfp_tx_entry {
uint8_t wire[NRFP_FRAME_WIRE_MAX];
size_t wire_len;
uint8_t channel;
uint8_t seq;
uint8_t retries;
uint8_t state;
uint64_t deadline_ms;
};

struct nrfp_tx_queue {
struct nrfp_tx_entry entries[NRFP_TX_QUEUE_DEPTH];
uint8_t head;
uint8_t tail;
uint8_t count;
uint8_t next_seq[NRFP_CHANNEL_COUNT];
};

uint16_t nrfp_crc16(const uint8_t *buf, size_t len);
int nrfp_frame_encode(uint8_t *out, size_t out_len, const struct nrfp_frame *frame, size_t *written);
int nrfp_frame_decode(const uint8_t *in, size_t in_len, struct nrfp_frame *frame, uint16_t *consumed_len,
      struct nrfp_rx_health *health);
int nrfp_frame_fragment(const struct nrfp_frame *src, uint16_t max_payload, struct nrfp_fragment_cursor *cursor,
uint8_t *fragment_payload, size_t fragment_payload_len, struct nrfp_frame *out,
bool *finished);
int nrfp_frame_aggregate(const struct nrfp_frame *frames, size_t frame_count, uint8_t *out, size_t out_len,
size_t *written, size_t *packed_count);

void nrfp_tx_queue_init(struct nrfp_tx_queue *queue);
int nrfp_tx_queue_push(struct nrfp_tx_queue *queue, const struct nrfp_frame *frame, uint64_t now_ms);
int nrfp_retry_scheduler(struct nrfp_tx_queue *queue, uint64_t now_ms, const struct nrfp_frame *rx,
 uint8_t *out, size_t out_len, size_t *written);

#endif /* NRFP_TRANSPORT_H */
