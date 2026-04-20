#include <stddef.h>
#include <stdint.h>

#include "../../common/include/nrfp_proto.h"
#include "tl_codec.h"

int nrfp_svc_nfc_handle(const struct nrfp_tl_frame *req, struct nrfp_tl_frame *rsp)
{
	if (!req || !rsp) {
		return -1;
	}

	rsp->hdr = req->hdr;
	rsp->hdr.flags |= NRFP_FLAG_IS_ACK;
	rsp->hdr.ack_seq = req->hdr.seq;
	rsp->hdr.status = NRFP_STATUS_UNSUPPORTED;
	rsp->hdr.payload_len_le = nrfp_u16_to_le(0);
	rsp->payload = NULL;

	switch (req->hdr.opcode) {
	case NRFP_OP_NFC_INIT:
	case NRFP_OP_NFC_MODE:
	case NRFP_OP_NFC_APDU_TX:
	case NRFP_OP_NFC_RAW_FRAME:
		rsp->hdr.status = NRFP_STATUS_OK;
		break;
	default:
		break;
	}

	return 0;
}

