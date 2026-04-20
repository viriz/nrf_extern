#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "nrfp_proto.h"
#include "nrfp_zynq_ioctl.h"

static int send_ping(int fd)
{
struct nrfp_zynq_frame_req req;
const char payload[] = "echo-from-zynq-user";

memset(&req, 0, sizeof(req));
	req.channel = NRFP_CH_CTRL;
	req.opcode = NRFP_OP_CTRL_PING;
	req.flags = NRFP_FLAG_ACK_REQ;
	req.payload_len = sizeof(payload) - 1;
memcpy(req.payload, payload, req.payload_len);

return ioctl(fd, NRFP_ZYNQ_IOC_SEND_FRAME, &req);
}

int main(int argc, char **argv)
{
const char *devnode = (argc > 1) ? argv[1] : "/dev/nrfp-zynq0";
struct nrfp_zynq_stats stats;
struct nrfp_zynq_event_msg evt;
struct pollfd pfd;
int fd;
int i;

fd = open(devnode, O_RDWR | O_NONBLOCK);
if (fd < 0) {
perror("open device");
return 1;
}

if (send_ping(fd) < 0)
fprintf(stderr, "send ping failed: %s\n", strerror(errno));
else
printf("ping/echo request queued via %s\n", devnode);

memset(&pfd, 0, sizeof(pfd));
pfd.fd = fd;
pfd.events = POLLIN;

for (i = 0; i < 5; i++) {
int pr = poll(&pfd, 1, 1000);

if (pr < 0) {
perror("poll");
break;
}
if (pr == 0)
continue;
if (read(fd, &evt, sizeof(evt)) == (ssize_t)sizeof(evt)) {
printf("event[%d]: ch=%u op=0x%02x status=%u seq=%u len=%u ts=%llu\n",
       i,
       evt.channel,
       evt.opcode,
       evt.status,
       evt.seq,
       evt.payload_len,
       (unsigned long long)evt.timestamp_ns);
}
}

if (ioctl(fd, NRFP_ZYNQ_IOC_GET_STATS, &stats) == 0) {
printf("stats: tx_frames=%llu tx_bytes=%llu rx_events=%llu irq=%llu dropped=%llu\n",
       (unsigned long long)stats.tx_frames,
       (unsigned long long)stats.tx_bytes,
       (unsigned long long)stats.rx_events,
       (unsigned long long)stats.irq_count,
       (unsigned long long)stats.rx_dropped);
printf("       rx_frames=%llu crc_errors=%llu pl_rx_overflow=%llu pl_tx_overflow=%llu\n",
       (unsigned long long)stats.rx_frames,
       (unsigned long long)stats.crc_errors,
       (unsigned long long)stats.pl_rx_overflow,
       (unsigned long long)stats.pl_tx_overflow);
}

close(fd);
return 0;
}
