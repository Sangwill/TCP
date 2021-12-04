#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

// global state
struct sockaddr_un remote_addr;
int socket_fd = 0;
FILE *pcap_fp = nullptr;

// random packet drop
// never drop by default
int recv_drop_numerator = 0;
int recv_drop_denominator = 1;
int send_drop_numerator = 0;
int send_drop_denominator = 1;

// taken from https://stackoverflow.com/a/1549344
bool set_socket_blocking(int fd, bool blocking) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    return false;
  flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
  return fcntl(fd, F_SETFL, flags) == 0;
}

struct sockaddr_un create_sockaddr_un(const char *path) {
  struct sockaddr_un addr = {};
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path));
  return addr;
}

int setup_unix_socket(const char *path) {
  // remove if exists
  unlink(path);
  int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0) {
    perror("Failed to create unix datagram socket");
    exit(1);
  }

  struct sockaddr_un addr = create_sockaddr_un(path);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Failed to create bind local datagram socket");
    exit(1);
  }
  set_socket_blocking(fd, false);
  return fd;
}

FILE *pcap_create(const char *path) {
  FILE *fp = fopen(path, "wb");

  pcap_header_t header;
  header.magic_number = 0xa1b2c3d4;
  header.version_major = 2;
  header.version_minor = 4;
  header.thiszone = 0;
  header.sigfigs = 0;
  header.snaplen = 65535;
  header.network = 0x65; // Raw

  fwrite(&header, sizeof(header), 1, fp);
  fflush(fp);
  return fp;
}

void pcap_write(FILE *fp, const uint8_t *data, size_t size) {

  pcap_packet_header_t header;
  header.caplen = size;
  header.len = size;

  struct timespec tp = {};
  clock_gettime(CLOCK_MONOTONIC, &tp);
  header.tv_sec = tp.tv_sec;
  header.tv_usec = tp.tv_nsec / 1000;

  fwrite(&header, sizeof(header), 1, fp);
  fwrite(data, size, 1, fp);
  fflush(fp);
}

void send_packet(const uint8_t *data, size_t size) {
  printf("TX:");
  for (size_t i = 0; i < size; i++) {
    printf(" %02X", data[i]);
  }
  printf("\n");

  if (rand() % send_drop_denominator < send_drop_numerator) {
    printf("Send packet dropped\n");
    return;
  }

  // save to pcap
  if (pcap_fp) {
    pcap_write(pcap_fp, data, size);
  }

  // send to remote
  sendto(socket_fd, data, size, 0, (struct sockaddr *)&remote_addr,
         sizeof(remote_addr));
}

ssize_t recv_packet(uint8_t *buffer, size_t buffer_size) {
  struct sockaddr_un addr = {};
  memset(&addr, 0, sizeof(addr));
  socklen_t len = sizeof(addr);
  ssize_t size = recvfrom(socket_fd, buffer, buffer_size, 0,
                          (struct sockaddr *)&addr, &len);
  if (size >= 0) {
    // print
    printf("RX:");
    for (ssize_t i = 0; i < size; i++) {
      printf(" %02X", buffer[i]);
    }
    printf("\n");

    if (rand() % recv_drop_denominator < recv_drop_numerator) {
      printf("Recv packet dropped\n");
      return -1;
    }

    // pcap
    if (pcap_fp) {
      pcap_write(pcap_fp, buffer, size);
    }
  }
  return size;
}

void parse_argv(int argc, char *argv[]) {
  int c;
  int hflag = 0;
  char *local = NULL;
  char *remote = NULL;
  char *pcap = NULL;

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
      exit(1);
      break;
    default:
      break;
    }
  }

  if (hflag) {
    fprintf(stderr, "Usage: %s [-h] [-l LOCAL] [-r REMOTE]\n", argv[0]);
    fprintf(stderr, "\t-l LOCAL: local unix socket path\n");
    fprintf(stderr, "\t-r REMOTE: remote unix socket path\n");
    fprintf(stderr, "\t-p PCAP: pcap file for debugging\n");
    exit(0);
  }

  if (!local) {
    fprintf(stderr, "Please specify LOCAL addr(-l)!\n");
    exit(1);
  }
  if (!remote) {
    fprintf(stderr, "Please specify REMOTE addr(-r)!\n");
    exit(1);
  }

  printf("Using local addr: %s\n", local);
  printf("Using remote addr: %s\n", remote);

  remote_addr = create_sockaddr_un(remote);

  socket_fd = setup_unix_socket(local);

  if (pcap) {
    pcap_fp = pcap_create(pcap);
  }
}