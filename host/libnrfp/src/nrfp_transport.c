#include "nrfp_transport.h"

#include <errno.h>
#include <string.h>

#define NRFP_TX_STATE_FREE 0u
#define NRFP_TX_STATE_QUEUED 1u
#define NRFP_TX_STATE_WAIT_ACK 2u

static size_t nrfp_payload_len(const struct nrfp_frame *frame)
{
return nrfp_u16_from_le(frame->hdr.payload_len_le);
}

static int nrfp_encode_internal(uint8_t *out, size_t out_len, const struct nrfp_tl_header *hdr,
const uint8_t *payload, size_t payload_len, size_t *written)
{
size_t total_wo_crc;
uint16_t crc;

if (!out || !hdr || !written)
return -EINVAL;
if (payload_len > NRFP_TL_MAX_PAYLOAD)
return -EMSGSIZE;
total_wo_crc = sizeof(*hdr) + payload_len;
if (out_len < total_wo_crc + sizeof(uint16_t))
return -ENOSPC;
if (payload_len && !payload)
return -EINVAL;

memcpy(out, hdr, sizeof(*hdr));
if (payload_len)
memcpy(out + sizeof(*hdr), payload, payload_len);
crc = nrfp_crc16(out, total_wo_crc);
out[total_wo_crc] = (uint8_t)(crc & 0xFFu);
out[total_wo_crc + 1u] = (uint8_t)(crc >> 8);
*written = total_wo_crc + sizeof(uint16_t);
return 0;
}

static void nrfp_pop_head_if_free(struct nrfp_tx_queue *queue)
{
while (queue->count && queue->entries[queue->head].state == NRFP_TX_STATE_FREE) {
queue->head = (uint8_t)((queue->head + 1u) % NRFP_TX_QUEUE_DEPTH);
queue->count--;
}
}

static void nrfp_set_retry_flag_and_crc(struct nrfp_tx_entry *entry)
{
uint16_t crc;
size_t crc_off = entry->wire_len - sizeof(uint16_t);

entry->wire[2] |= NRFP_FLAG_RETRY;
crc = nrfp_crc16(entry->wire, crc_off);
entry->wire[crc_off] = (uint8_t)(crc & 0xFFu);
entry->wire[crc_off + 1u] = (uint8_t)(crc >> 8);
}

static void nrfp_apply_rx_ack(struct nrfp_tx_queue *queue, const struct nrfp_frame *rx, uint64_t now_ms)
{
uint8_t channel;
uint8_t i;
bool force_retry;

if (!rx)
return;
channel = nrfp_channel_from_flags(rx->hdr.flags);
if (channel >= NRFP_CHANNEL_COUNT)
return;
if (!(rx->hdr.flags & (NRFP_FLAG_IS_ACK | NRFP_FLAG_IS_NAK)))
return;
force_retry = ((rx->hdr.flags & NRFP_FLAG_IS_NAK) != 0u);
for (i = 0; i < NRFP_TX_QUEUE_DEPTH; i++) {
struct nrfp_tx_entry *entry = &queue->entries[i];

if (entry->state != NRFP_TX_STATE_WAIT_ACK || entry->channel != channel)
continue;
if (entry->seq != rx->hdr.ack_seq)
continue;
if (force_retry) {
entry->deadline_ms = now_ms;
} else {
entry->state = NRFP_TX_STATE_FREE;
}
break;
}
nrfp_pop_head_if_free(queue);
}

uint16_t nrfp_crc16(const uint8_t *buf, size_t len)
{
return nrfp_crc16_ccitt_false(buf, len);
}

int nrfp_frame_encode(uint8_t *out, size_t out_len, const struct nrfp_frame *frame, size_t *written)
{
size_t payload_len;

if (!frame)
return -EINVAL;
payload_len = nrfp_payload_len(frame);
if (frame->hdr.version != NRFP_TL_VERSION || frame->hdr.sof != NRFP_TL_SOF)
return -EINVAL;
if (nrfp_channel_from_flags(frame->hdr.flags) >= NRFP_CHANNEL_COUNT)
return -EINVAL;
if (nrfp_channel_from_flags(frame->hdr.flags) == NRFP_CH_AUDIO &&
    frame->hdr.opcode == NRFP_OP_AUDIO_FMT && payload_len > 0u &&
    frame->payload && frame->payload[0] == NRFP_AUDIO_FMT_PCM_LE16)
return -EOPNOTSUPP;
return nrfp_encode_internal(out, out_len, &frame->hdr, frame->payload, payload_len, written);
}

int nrfp_frame_decode(const uint8_t *in, size_t in_len, struct nrfp_frame *frame, uint16_t *consumed_len,
      struct nrfp_rx_health *health)
{
uint16_t payload_len;
size_t total_wo_crc;
size_t total;
uint16_t expected_crc;
uint16_t got_crc;

if (!in || !frame || !consumed_len || in_len < sizeof(struct nrfp_tl_header) + sizeof(uint16_t))
return -EINVAL;

memcpy(&frame->hdr, in, sizeof(frame->hdr));
if (frame->hdr.sof != NRFP_TL_SOF || frame->hdr.version != NRFP_TL_VERSION)
return -EPROTO;
if (nrfp_channel_from_flags(frame->hdr.flags) >= NRFP_CHANNEL_COUNT)
return -EPROTO;

payload_len = nrfp_u16_from_le(frame->hdr.payload_len_le);
if (payload_len > NRFP_TL_MAX_PAYLOAD)
return -EMSGSIZE;
total_wo_crc = sizeof(struct nrfp_tl_header) + payload_len;
total = total_wo_crc + sizeof(uint16_t);
if (in_len < total)
return -ENODATA;
got_crc = (uint16_t)in[total_wo_crc] | ((uint16_t)in[total_wo_crc + 1u] << 8);
expected_crc = nrfp_crc16(in, total_wo_crc);
if (got_crc != expected_crc) {
if (health) {
if (health->crc_error_streak < NRFP_CRC_ERROR_RESET_THRESHOLD)
health->crc_error_streak++;
if (health->crc_error_streak >= NRFP_CRC_ERROR_RESET_THRESHOLD)
health->crc_error_streak = 0;
}
return -EBADMSG;
}
if (health)
health->crc_error_streak = 0;
frame->payload = payload_len ? (in + sizeof(struct nrfp_tl_header)) : NULL;
*consumed_len = (uint16_t)total;
return 0;
}

int nrfp_frame_fragment(const struct nrfp_frame *src, uint16_t max_payload, struct nrfp_fragment_cursor *cursor,
uint8_t *fragment_payload, size_t fragment_payload_len, struct nrfp_frame *out,
bool *finished)
{
size_t payload_len;
size_t mtu_data;
size_t left;
size_t chunk;

if (!src || !cursor || !fragment_payload || !out || !finished)
return -EINVAL;
payload_len = nrfp_payload_len(src);
if (payload_len <= max_payload) {
*out = *src;
*finished = true;
return 0;
}
if (max_payload <= NRFP_FRAGMENT_META_SIZE)
return -EMSGSIZE;
if (!src->payload)
return -EINVAL;
mtu_data = max_payload - NRFP_FRAGMENT_META_SIZE;
if (!cursor->total)
cursor->total = (uint8_t)((payload_len + mtu_data - 1u) / mtu_data);
if (cursor->offset >= payload_len)
return -ERANGE;
left = payload_len - cursor->offset;
chunk = left > mtu_data ? mtu_data : left;
if (fragment_payload_len < chunk + NRFP_FRAGMENT_META_SIZE)
return -ENOSPC;
fragment_payload[0] = cursor->index;
fragment_payload[1] = cursor->total;
fragment_payload[2] = (uint8_t)(payload_len & 0xFFu);
fragment_payload[3] = (uint8_t)(payload_len >> 8);
memcpy(fragment_payload + NRFP_FRAGMENT_META_SIZE, src->payload + cursor->offset, chunk);
*out = *src;
out->payload = fragment_payload;
out->hdr.payload_len_le = nrfp_u16_to_le((uint16_t)(chunk + NRFP_FRAGMENT_META_SIZE));
cursor->offset = (uint16_t)(cursor->offset + chunk);
cursor->index++;
*finished = (cursor->offset >= payload_len);
return 0;
}

int nrfp_frame_aggregate(const struct nrfp_frame *frames, size_t frame_count, uint8_t *out, size_t out_len,
size_t *written, size_t *packed_count)
{
size_t i;
size_t used = 0;
size_t packed = 0;
int rc;

if ((!frames && frame_count) || !out || !written || !packed_count)
return -EINVAL;
for (i = 0; i < frame_count; i++) {
size_t one = 0;
const struct nrfp_frame *frame = &frames[i];
uint8_t flags = frame->hdr.flags;
size_t payload_len = nrfp_payload_len(frame);
bool is_idle_fill = (payload_len == 0u) && ((flags & (NRFP_FLAG_REQ_ACK | NRFP_FLAG_IS_ACK | NRFP_FLAG_IS_NAK)) == 0u);

if (is_idle_fill)
continue;
rc = nrfp_frame_encode(out + used, out_len - used, frame, &one);
if (rc)
return rc;
used += one;
packed++;
}
*written = used;
*packed_count = packed;
return 0;
}

void nrfp_tx_queue_init(struct nrfp_tx_queue *queue)
{
if (!queue)
return;
memset(queue, 0, sizeof(*queue));
}

int nrfp_tx_queue_push(struct nrfp_tx_queue *queue, const struct nrfp_frame *frame, uint64_t now_ms)
{
struct nrfp_tx_entry *entry;
struct nrfp_frame local;
size_t written;
uint8_t channel;
int rc;

if (!queue || !frame)
return -EINVAL;
if (queue->count >= NRFP_TX_QUEUE_DEPTH)
return -ENOSPC;
channel = nrfp_channel_from_flags(frame->hdr.flags);
if (channel >= NRFP_CHANNEL_COUNT)
return -EINVAL;

local = *frame;
local.hdr.seq = queue->next_seq[channel]++;
entry = &queue->entries[queue->tail];
memset(entry, 0, sizeof(*entry));
rc = nrfp_frame_encode(entry->wire, sizeof(entry->wire), &local, &written);
if (rc)
return rc;
entry->wire_len = written;
entry->channel = channel;
entry->seq = local.hdr.seq;
entry->deadline_ms = now_ms + NRFP_RETRY_TIMEOUT_MS;
entry->state = NRFP_TX_STATE_QUEUED;
queue->tail = (uint8_t)((queue->tail + 1u) % NRFP_TX_QUEUE_DEPTH);
queue->count++;
return 0;
}

int nrfp_retry_scheduler(struct nrfp_tx_queue *queue, uint64_t now_ms, const struct nrfp_frame *rx,
 uint8_t *out, size_t out_len, size_t *written)
{
uint8_t i;

if (!queue || !out || !written)
return -EINVAL;

nrfp_apply_rx_ack(queue, rx, now_ms);

for (i = 0; i < NRFP_TX_QUEUE_DEPTH; i++) {
struct nrfp_tx_entry *entry = &queue->entries[(uint8_t)((queue->head + i) % NRFP_TX_QUEUE_DEPTH)];
if (entry->state != NRFP_TX_STATE_QUEUED)
continue;
if (out_len < entry->wire_len)
return -ENOSPC;
memcpy(out, entry->wire, entry->wire_len);
*written = entry->wire_len;
if (entry->wire[2] & NRFP_FLAG_REQ_ACK) {
entry->state = NRFP_TX_STATE_WAIT_ACK;
entry->deadline_ms = now_ms + NRFP_RETRY_TIMEOUT_MS;
} else {
entry->state = NRFP_TX_STATE_FREE;
nrfp_pop_head_if_free(queue);
}
return 0;
}

for (i = 0; i < NRFP_TX_QUEUE_DEPTH; i++) {
struct nrfp_tx_entry *entry = &queue->entries[(uint8_t)((queue->head + i) % NRFP_TX_QUEUE_DEPTH)];
if (entry->state != NRFP_TX_STATE_WAIT_ACK || now_ms < entry->deadline_ms)
continue;
if (entry->retries >= NRFP_RETRY_MAX_ATTEMPTS)
return -ETIMEDOUT;
if (out_len < entry->wire_len)
return -ENOSPC;
nrfp_set_retry_flag_and_crc(entry);
entry->retries++;
entry->deadline_ms = now_ms + NRFP_RETRY_TIMEOUT_MS;
memcpy(out, entry->wire, entry->wire_len);
*written = entry->wire_len;
return 0;
}
return -EAGAIN;
}
