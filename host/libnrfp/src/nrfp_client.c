#include "nrfp_client.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "nrfp_transport.h"

int nrfp_client_open_channel(uint8_t channel)
{
	char path[32];

	(void)snprintf(path, sizeof(path), "/dev/nrfp-ch%u", channel);
	return open(path, O_RDWR | O_CLOEXEC);
}

void nrfp_client_close(int fd)
{
	if (fd >= 0)
		close(fd);
}

int nrfp_client_request(int fd, uint8_t opcode, const uint8_t *payload, size_t len)
{
	struct nrfp_frame frame;
	struct nrfp_tx_queue tx_queue;
	uint8_t wire[NRFP_FRAME_WIRE_MAX];
	size_t wire_len;
	ssize_t wrote;
	int saved_errno;
	int rc;

	if (fd < 0)
		return -EBADF;
	nrfp_tx_queue_init(&tx_queue);
	if (len > NRFP_TL_MAX_PAYLOAD)
		return -EMSGSIZE;
	memset(&frame, 0, sizeof(frame));
	frame.hdr.sof = NRFP_TL_SOF;
	frame.hdr.version = NRFP_TL_VERSION;
	frame.hdr.flags = (uint8_t)(NRFP_CH_CTRL | NRFP_FLAG_REQ_ACK);
	frame.hdr.opcode = opcode;
	frame.hdr.status = NRFP_STATUS_OK;
	frame.hdr.payload_len_le = nrfp_u16_to_le((uint16_t)len);
	frame.payload = payload;
	rc = nrfp_tx_queue_push(&tx_queue, &frame, 0u);
	if (rc)
		return rc;
	rc = nrfp_retry_scheduler(&tx_queue, 0u, NULL, wire, sizeof(wire), &wire_len);
	if (rc)
		return rc;
	wrote = write(fd, wire, wire_len);
	saved_errno = errno;
	if (wrote < 0)
		return -saved_errno;
	if ((size_t)wrote != wire_len)
		return -EIO;
	return 0;
}
