#include "nrfp_client.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

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
	(void)opcode;
	(void)payload;
	(void)len;

	if (fd < 0)
		return -EBADF;
	return -ENOTSUP;
}
