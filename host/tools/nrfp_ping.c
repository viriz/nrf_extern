#include <errno.h>
#include <stdio.h>

#include "nrfp_client.h"
#include "nrfp_proto.h"

int main(void)
{
	int fd = nrfp_client_open_channel(NRFP_CH_CTRL);
	int rc;
	if (fd < 0) {
		perror("open /dev/nrfp-ch0");
		return 1;
	}

	rc = nrfp_client_request(fd, NRFP_OP_CTRL_PING, NULL, 0);
	if (rc == -ENOTSUP) {
		printf("ping request API present (transport write path not implemented in scaffold)\n");
		nrfp_client_close(fd);
		return 0;
	}

	if (rc != 0) {
		fprintf(stderr, "ping request failed\n");
		nrfp_client_close(fd);
		return 2;
	}

	printf("ping request queued\n");
	nrfp_client_close(fd);
	return 0;
}
