#include "tl_frame.h"

uint16_t nrfp_crc16(const uint8_t *buf, size_t len)
{
return nrfp_crc16_ccitt_false(buf, len);
}
