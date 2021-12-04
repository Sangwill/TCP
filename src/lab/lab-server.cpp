#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <assert.h>

#include "common.h"
#include "ip.h"
#include "tcp.h"
#include "timers.h"

int main(int argc, char *argv[]) {
  parse_argv(argc, argv);

  int listen_fd = tcp_socket();
  tcp_bind(listen_fd, server_ip, 80);
  tcp_listen(listen_fd);
  printf("Listening on 80 port\n");

  timer_fn accept_fn = [=]() {
    int new_fd = tcp_accept(listen_fd);
    if (new_fd >= 0) {
      printf("Got new TCP connection: fd=%d\n", new_fd);
      timer_fn server_fn = [=]() {
        char buffer[1024];
        ssize_t res = tcp_read(new_fd, (uint8_t *)buffer, sizeof(buffer) - 1);
        if (res > 0) {
          printf("Read '");
          fwrite(buffer, res, 1, stdout);
          printf("' from tcp\n");

          // send something back
          sprintf(buffer, "%zd", res);
          ssize_t res = tcp_write(new_fd, (const uint8_t *)buffer, strlen(buffer));
          assert(res > 0);
        }

        // next data
        return 1000;
      };

      TIMERS.schedule_job(server_fn, 1000);
    }

    // next accept
    return 1000;
  };
  TIMERS.schedule_job(accept_fn, 1000);

  // main loop
  const size_t buffer_size = 1500; // MTU
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