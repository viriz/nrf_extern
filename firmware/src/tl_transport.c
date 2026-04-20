#include "tl_codec.h"

#include <string.h>

struct nrfp_channel_state {
	uint8_t expected_rx_seq;
	uint8_t next_tx_seq;
	uint8_t last_ack_seq;
};

static struct nrfp_channel_state channel_state[8];

void nrfp_tl_transport_reset(void)
{
	memset(channel_state, 0, sizeof(channel_state));
}

uint8_t nrfp_tl_transport_next_seq(uint8_t channel)
{
	if (channel >= 8) {
		return 0;
	}
	return channel_state[channel].next_tx_seq++;
}

int nrfp_tl_transport_accept_seq(uint8_t channel, uint8_t seq, uint8_t retry)
{
	struct nrfp_channel_state *s;

	if (channel >= 8) {
		return -1;
	}

	s = &channel_state[channel];
	if (seq == s->expected_rx_seq) {
		s->last_ack_seq = seq;
		s->expected_rx_seq++;
		return 0;
	}

	if (retry && seq == (uint8_t)(s->expected_rx_seq - 1u)) {
		return 1;
	}

	return -2;
}

