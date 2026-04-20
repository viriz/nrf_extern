#include "tl_codec.h"

int nrfp_tl_encode(uint8_t *out, size_t out_len, const struct nrfp_tl_frame *frame, size_t *written)
{
	return nrfp_frame_encode(out, out_len, frame, written);
}

int nrfp_tl_decode(const uint8_t *in, size_t in_len, struct nrfp_tl_frame *frame, uint16_t *consumed_len)
{
	return nrfp_frame_decode(in, in_len, frame, consumed_len);
}
