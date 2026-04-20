#include <stdio.h>

#include "nrfp_client.h"
#include "nrfp_proto.h"

int main(void)
{
	printf("nrfp-daemon scaffold starting (passive nRF endpoint, host policy owner)\n");
	printf("max connections target: %u, default audio format: LC3_RAW\n", NRFP_MAX_CONNECTIONS);
	return 0;
}
