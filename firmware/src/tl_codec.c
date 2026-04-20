#include "tl_codec.h"

#include <string.h>

int nrfp_tl_encode(uint8_t *out, size_t out_len, const struct nrfp_tl_frame *frame, size_t *written)
{
	size_t payload_len;
	size_t total_wo_crc;
	uint16_t crc;

	if (!out || !frame || !written) {
		return -1;
	}

	payload_len = nrfp_u16_from_le(frame->hdr.payload_len_le);
	total_wo_crc = sizeof(struct nrfp_tl_header) + payload_len;
	if (payload_len > NRFP_TL_MAX_PAYLOAD || out_len < total_wo_crc + sizeof(uint16_t)) {
		return -2;
	}

	memcpy(out, &frame->hdr, sizeof(frame->hdr));
	if (payload_len && frame->payload) {
		memcpy(out + sizeof(frame->hdr), frame->payload, payload_len);
	}
	crc = nrfp_crc16_ccitt_false(out, total_wo_crc);
	out[total_wo_crc] = (uint8_t)(crc & 0xFFu);
	out[total_wo_crc + 1u] = (uint8_t)(crc >> 8);
	*written = total_wo_crc + sizeof(uint16_t);

	return 0;
}

int nrfp_tl_decode(const uint8_t *in, size_t in_len, struct nrfp_tl_frame *frame, uint16_t *consumed_len)
{
	uint16_t payload_len;
	size_t total_wo_crc;
	size_t total;
	uint16_t expected_crc;
	uint16_t got_crc;

	if (!in || !frame || !consumed_len || in_len < sizeof(struct nrfp_tl_header) + sizeof(uint16_t)) {
		return -1;
	}

	memcpy(&frame->hdr, in, sizeof(frame->hdr));
	if (frame->hdr.sof != NRFP_TL_SOF || frame->hdr.version != NRFP_TL_VERSION) {
		return -2;
	}

	payload_len = nrfp_u16_from_le(frame->hdr.payload_len_le);
	if (payload_len > NRFP_TL_MAX_PAYLOAD) {
		return -3;
	}

	total_wo_crc = sizeof(struct nrfp_tl_header) + payload_len;
	total = total_wo_crc + sizeof(uint16_t);
	if (in_len < total) {
		return -4;
	}

	got_crc = (uint16_t)in[total_wo_crc] | ((uint16_t)in[total_wo_crc + 1u] << 8);
	expected_crc = nrfp_crc16_ccitt_false(in, total_wo_crc);
	if (got_crc != expected_crc) {
		return -5;
	}

	frame->payload = payload_len ? (in + sizeof(struct nrfp_tl_header)) : NULL;
	*consumed_len = (uint16_t)total;

	return 0;
}

