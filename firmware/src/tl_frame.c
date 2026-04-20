#include "tl_frame.h"

#include <string.h>

static size_t nrfp_fw_payload_len(const struct nrfp_tl_frame *frame)
{
return nrfp_u16_from_le(frame->hdr.payload_len_le);
}

int nrfp_frame_encode(uint8_t *out, size_t out_len, const struct nrfp_tl_frame *frame, size_t *written)
{
size_t payload_len;
size_t total_wo_crc;
uint16_t crc;

if (!out || !frame || !written)
return -1;
if (frame->hdr.sof != NRFP_TL_SOF || frame->hdr.version != NRFP_TL_VERSION)
return -2;
if (nrfp_channel_from_flags(frame->hdr.flags) >= NRFP_CHANNEL_COUNT)
return -3;
payload_len = nrfp_fw_payload_len(frame);
if (payload_len > NRFP_TL_MAX_PAYLOAD)
return -4;
total_wo_crc = sizeof(struct nrfp_tl_header) + payload_len;
if (out_len < total_wo_crc + sizeof(uint16_t))
return -5;
if (payload_len && !frame->payload)
return -6;

memcpy(out, &frame->hdr, sizeof(frame->hdr));
if (payload_len)
memcpy(out + sizeof(frame->hdr), frame->payload, payload_len);
crc = nrfp_crc16(out, total_wo_crc);
out[total_wo_crc] = (uint8_t)(crc & 0xFFu);
out[total_wo_crc + 1u] = (uint8_t)(crc >> 8);
*written = total_wo_crc + sizeof(uint16_t);
return 0;
}

int nrfp_frame_decode(const uint8_t *in, size_t in_len, struct nrfp_tl_frame *frame, uint16_t *consumed_len)
{
uint16_t payload_len;
size_t total_wo_crc;
size_t total;
uint16_t expected_crc;
uint16_t got_crc;

if (!in || !frame || !consumed_len || in_len < sizeof(struct nrfp_tl_header) + sizeof(uint16_t))
return -1;

memcpy(&frame->hdr, in, sizeof(frame->hdr));
if (frame->hdr.sof != NRFP_TL_SOF || frame->hdr.version != NRFP_TL_VERSION)
return -2;
if (nrfp_channel_from_flags(frame->hdr.flags) >= NRFP_CHANNEL_COUNT)
return -3;
payload_len = nrfp_u16_from_le(frame->hdr.payload_len_le);
if (payload_len > NRFP_TL_MAX_PAYLOAD)
return -4;
total_wo_crc = sizeof(struct nrfp_tl_header) + payload_len;
total = total_wo_crc + sizeof(uint16_t);
if (in_len < total)
return -5;
got_crc = (uint16_t)in[total_wo_crc] | ((uint16_t)in[total_wo_crc + 1u] << 8);
expected_crc = nrfp_crc16(in, total_wo_crc);
if (got_crc != expected_crc)
return -6;
frame->payload = payload_len ? (in + sizeof(struct nrfp_tl_header)) : NULL;
*consumed_len = (uint16_t)total;
return 0;
}

int nrfp_frame_fragment(const struct nrfp_tl_frame *src, uint16_t max_payload,
struct nrfp_fw_fragment_cursor *cursor, uint8_t *fragment_payload,
size_t fragment_payload_len, struct nrfp_tl_frame *out, bool *finished)
{
size_t payload_len;
size_t mtu_data;
size_t left;
size_t chunk;

if (!src || !cursor || !fragment_payload || !out || !finished)
return -1;
payload_len = nrfp_fw_payload_len(src);
if (payload_len <= max_payload) {
*out = *src;
*finished = true;
return 0;
}
if (max_payload <= NRFP_FW_FRAGMENT_META_SIZE)
return -2;
if (!src->payload)
return -3;
mtu_data = max_payload - NRFP_FW_FRAGMENT_META_SIZE;
if (!cursor->total)
cursor->total = (uint8_t)((payload_len + mtu_data - 1u) / mtu_data);
if (cursor->offset >= payload_len)
return -4;
left = payload_len - cursor->offset;
chunk = left > mtu_data ? mtu_data : left;
if (fragment_payload_len < chunk + NRFP_FW_FRAGMENT_META_SIZE)
return -5;
fragment_payload[0] = cursor->index;
fragment_payload[1] = cursor->total;
fragment_payload[2] = (uint8_t)(payload_len & 0xFFu);
fragment_payload[3] = (uint8_t)(payload_len >> 8);
memcpy(fragment_payload + NRFP_FW_FRAGMENT_META_SIZE, src->payload + cursor->offset, chunk);
*out = *src;
out->payload = fragment_payload;
out->hdr.payload_len_le = nrfp_u16_to_le((uint16_t)(chunk + NRFP_FW_FRAGMENT_META_SIZE));
cursor->offset = (uint16_t)(cursor->offset + chunk);
cursor->index++;
*finished = (cursor->offset >= payload_len);
return 0;
}
