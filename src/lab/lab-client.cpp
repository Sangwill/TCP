#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "common.h"
#include "ip.h"
#include "tcp.h"
#include "timers.h"

int main(int argc, char *argv[]) {
  parse_argv(argc, argv);

  int tcp_fd = tcp_socket();
  tcp_connect(tcp_fd, server_ip, 80);

  // always try to read
  std::function<void()> read_fn = [&] {
    char buffer[1024];
    ssize_t res = tcp_read(tcp_fd, (uint8_t *)buffer, sizeof(buffer) - 1);
    if (res > 0) {
      printf("Read '");
      fwrite(buffer, res, 1, stdout);
      printf("' from tcp\n");
    }

    // next data
    TIMERS.schedule_job(read_fn, 1000);
  };
  TIMERS.schedule_job(read_fn, 1000);

  // write something every 1s
  const char *data[] = {
      "GET /index.html HTTP/1.1\r\n",
      "Accept: */*\r\n",
      "Host: 10.0.0.1\r\n",
      "Connection: Close\r\n",
      "\r\n",
  };
  int index = 0;
  int offset = 0;
  std::function<void()> write_fn = [&] {
    const char *p = data[index];
    size_t len = strlen(p);
    ssize_t res = tcp_write(tcp_fd, (const uint8_t *)p + offset, len - offset);
    if (res > 0) {
      printf("Write '%s' to tcp\n", p);
      offset += res;

      // write completed
      if (offset == len) {
        index++;
        offset = 0;
      }
    }

    // next data
    if (index < 5) {
      TIMERS.schedule_job(write_fn, 1000);
    }
  };
  TIMERS.schedule_job(write_fn, 1000);

  // main loop
  const size_t buffer_size = 1500;
  uint8_t buffer[buffer_size];
  while (1) {
    ssize_t size = recv_packet(buffer, buffer_size);
    if (size >= 0) {
      // got data
      process_ip(buffer, size);
    }
    TIMERS.trigger();
  }
  return 0;
}
