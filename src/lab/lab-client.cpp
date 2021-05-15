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
  int c;
  char *local = NULL;
  char *remote = NULL;
  char *pcap = NULL;
  int hflag = 0;

  // parse arguments
  while ((c = getopt(argc, argv, "hl:r:p:")) != -1) {
    switch (c) {
    case 'h':
      hflag = 1;
      break;
    case 'l':
      local = optarg;
      break;
    case 'r':
      remote = optarg;
      break;
    case 'p':
      pcap = optarg;
      break;
    case '?':
      fprintf(stderr, "Unknown option: %c\n", optopt);
      return 1;
    default:
      break;
    }
  }

  if (hflag) {
    fprintf(stderr, "Usage: %s [-h] [-l LOCAL] [-r REMOTE]\n", argv[0]);
    fprintf(stderr, "\t-l LOCAL: local unix socket path\n");
    fprintf(stderr, "\t-r REMOTE: remote unix socket path\n");
    fprintf(stderr, "\t-p PCAP: pcap file for debugging\n");
    return 0;
  }
  if (!local) {
    fprintf(stderr, "Please specify LOCAL addr(-l)!\n");
    return 1;
  }
  if (!remote) {
    fprintf(stderr, "Please specify REMOTE addr(-r)!\n");
    return 1;
  }
  printf("Using local addr: %s\n", local);
  printf("Using remote addr: %s\n", remote);

  remote_addr = create_sockaddr_un(remote);

  socket_fd = setup_unix_socket(local);

  if (pcap) {
    pcap_fp = pcap_create(pcap);
  }

  srand(current_ts_msec());

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
      if (res == len) {
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
