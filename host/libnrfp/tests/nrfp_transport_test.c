#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nrfp_transport.h"

static struct nrfp_frame make_frame(uint8_t channel, uint8_t opcode, const uint8_t *payload, uint16_t len,
    uint8_t flags)
{
struct nrfp_frame frame;

memset(&frame, 0, sizeof(frame));
frame.hdr.sof = NRFP_TL_SOF;
frame.hdr.version = NRFP_TL_VERSION;
frame.hdr.flags = (uint8_t)((channel & NRFP_FLAG_CHANNEL_MASK) | flags);
frame.hdr.opcode = opcode;
frame.hdr.status = NRFP_STATUS_OK;
frame.hdr.payload_len_le = nrfp_u16_to_le(len);
frame.payload = payload;
return frame;
}

int main(void)
{
static const uint8_t crc_vec[] = "123456789";
uint8_t wire[NRFP_FRAME_WIRE_MAX];
size_t wire_len;
struct nrfp_frame frame;
struct nrfp_frame decoded;
uint16_t consumed;
uint8_t payload[24];
uint8_t frag_buf[32];
struct nrfp_fragment_cursor frag = { 0 };
bool finished;
size_t aggregated;
size_t packed;
struct nrfp_tx_queue queue;
size_t scheduled;
struct nrfp_frame ack;
uint8_t rx_wire[NRFP_FRAME_WIRE_MAX];
size_t rx_wire_len;
int rc;
unsigned int i;

assert(nrfp_crc16(crc_vec, sizeof(crc_vec) - 1u) == 0x29B1u);

for (i = 0; i < sizeof(payload); i++)
payload[i] = (uint8_t)i;

frame = make_frame(NRFP_CH_CTRL, NRFP_OP_CTRL_PING, payload, 8u, NRFP_FLAG_REQ_ACK);
assert(nrfp_frame_encode(wire, sizeof(wire), &frame, &wire_len) == 0);
assert(nrfp_frame_decode(wire, wire_len, &decoded, &consumed, NULL) == 0);
assert(consumed == wire_len);
assert(decoded.hdr.opcode == frame.hdr.opcode);
assert(decoded.hdr.flags == frame.hdr.flags);
assert(memcmp(decoded.payload, frame.payload, 8u) == 0);

frame = make_frame(NRFP_CH_BLE_DATA, NRFP_OP_BLE_DATA_TX, payload, sizeof(payload), 0);
assert(nrfp_frame_fragment(&frame, 10u, &frag, frag_buf, sizeof(frag_buf), &decoded, &finished) == 0);
assert(!finished);
assert(nrfp_u16_from_le(decoded.hdr.payload_len_le) == 10u);
assert(decoded.payload[0] == 0u);
assert(decoded.payload[1] > 1u);
while (!finished)
assert(nrfp_frame_fragment(&frame, 10u, &frag, frag_buf, sizeof(frag_buf), &decoded, &finished) == 0);

{
struct nrfp_frame list[2];
list[0] = make_frame(NRFP_CH_CTRL, 0u, NULL, 0u, 0u);
list[1] = make_frame(NRFP_CH_CTRL, NRFP_OP_CTRL_PING, payload, 2u, 0u);
assert(nrfp_frame_aggregate(list, 2u, wire, sizeof(wire), &aggregated, &packed) == 0);
assert(packed == 1u);
assert(aggregated > 0u);
}

nrfp_tx_queue_init(&queue);
frame = make_frame(NRFP_CH_CTRL, NRFP_OP_CTRL_PING, payload, 1u, NRFP_FLAG_REQ_ACK);
assert(nrfp_tx_queue_push(&queue, &frame, 0u) == 0);
assert(nrfp_retry_scheduler(&queue, 0u, NULL, wire, sizeof(wire), &scheduled) == 0);
assert(scheduled > 0u);

rc = nrfp_retry_scheduler(&queue, NRFP_RETRY_TIMEOUT_MS + 1u, NULL, wire, sizeof(wire), &scheduled);
assert(rc == 0);
assert((wire[2] & NRFP_FLAG_RETRY) != 0u);

ack = make_frame(NRFP_CH_CTRL, NRFP_OP_CTRL_PONG, NULL, 0u, NRFP_FLAG_IS_ACK);
ack.hdr.ack_seq = 0u;
assert(nrfp_frame_encode(rx_wire, sizeof(rx_wire), &ack, &rx_wire_len) == 0);
assert(nrfp_frame_decode(rx_wire, rx_wire_len, &decoded, &consumed, NULL) == 0);
assert(nrfp_retry_scheduler(&queue, NRFP_RETRY_TIMEOUT_MS + 2u, &decoded, wire, sizeof(wire), &scheduled) == -EAGAIN);

return 0;
}
