#ifndef NRFP_FW_TL_FRAME_H
#define NRFP_FW_TL_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/include/nrfp_proto.h"

#define NRFP_FW_FLAG_REQ_ACK NRFP_FLAG_ACK_REQ
#define NRFP_FW_FLAG_IS_ACK NRFP_FLAG_IS_ACK
#define NRFP_FW_FLAG_IS_NAK 0x80u
#define NRFP_FW_RETRY_TIMEOUT_MS 50u
#define NRFP_FW_RETRY_MAX_ATTEMPTS 3u
#define NRFP_FW_FRAGMENT_META_SIZE 4u

struct nrfp_tl_frame {
struct nrfp_tl_header hdr;
const uint8_t *payload;
};

struct nrfp_fw_fragment_cursor {
uint16_t offset;
uint8_t index;
uint8_t total;
};

uint16_t nrfp_crc16(const uint8_t *buf, size_t len);
int nrfp_frame_encode(uint8_t *out, size_t out_len, const struct nrfp_tl_frame *frame, size_t *written);
int nrfp_frame_decode(const uint8_t *in, size_t in_len, struct nrfp_tl_frame *frame, uint16_t *consumed_len);
int nrfp_frame_fragment(const struct nrfp_tl_frame *src, uint16_t max_payload,
		struct nrfp_fw_fragment_cursor *cursor, uint8_t *fragment_payload,
		size_t fragment_payload_len, struct nrfp_tl_frame *out, bool *finished);
int nrfp_frame_aggregate(const struct nrfp_tl_frame *frames, size_t frame_count, uint8_t *out,
			 size_t out_len, size_t *written, size_t *packed_count);

#endif /* NRFP_FW_TL_FRAME_H */
