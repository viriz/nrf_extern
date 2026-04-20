#ifndef NRFP_FW_TL_CODEC_H
#define NRFP_FW_TL_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "../../common/include/nrfp_proto.h"

struct nrfp_tl_frame {
	struct nrfp_tl_header hdr;
	const uint8_t *payload;
};

int nrfp_tl_encode(uint8_t *out, size_t out_len, const struct nrfp_tl_frame *frame, size_t *written);
int nrfp_tl_decode(const uint8_t *in, size_t in_len, struct nrfp_tl_frame *frame, uint16_t *consumed_len);

#endif /* NRFP_FW_TL_CODEC_H */

