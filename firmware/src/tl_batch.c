#include "tl_frame.h"

int nrfp_frame_aggregate(const struct nrfp_tl_frame *frames, size_t frame_count, uint8_t *out,
 size_t out_len, size_t *written, size_t *packed_count)
{
size_t i;
size_t used = 0;
size_t packed = 0;

if ((!frames && frame_count) || !out || !written || !packed_count)
return -1;
for (i = 0; i < frame_count; i++) {
size_t one;
uint8_t flags = frames[i].hdr.flags;
size_t payload_len = nrfp_u16_from_le(frames[i].hdr.payload_len_le);
int rc;

if (payload_len == 0u && (flags & (NRFP_FW_FLAG_REQ_ACK | NRFP_FLAG_IS_ACK | NRFP_FW_FLAG_IS_NAK)) == 0u)
continue;
rc = nrfp_frame_encode(out + used, out_len - used, &frames[i], &one);
if (rc)
return rc;
used += one;
packed++;
}
*written = used;
*packed_count = packed;
return 0;
}
