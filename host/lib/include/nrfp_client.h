#ifndef NRFP_CLIENT_H
#define NRFP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

int nrfp_client_open_channel(uint8_t channel);
void nrfp_client_close(int fd);
int nrfp_client_request(int fd, uint8_t opcode, const uint8_t *payload, size_t len);

#endif /* NRFP_CLIENT_H */
