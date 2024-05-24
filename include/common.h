#ifndef __COMMON_H__
#define __COMMON_H__

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <stdexcept>

// assume interface MTU is 1500 bytes
#define MTU 1500

// helper macros
#define UNIMPLEMENTED()                                                        \
  do {                                                                         \
    printf(RED "UNIMPLEMENTED at %s:%d\n" RESET, __FILE__, __LINE__);                    \
                                                                       \
  } while (0);

#define UNIMPLEMENTED_WARN()                                                   \
  do {                                                                         \
    printf(YEL "WARN: UNIMPLEMENTED at %s:%d\n" RESET, __FILE__, __LINE__);              \
  } while (0);

// useful types to mark endianness
// conversion between be{32,16}_t and other must use htons or ntohs
typedef uint32_t be32_t;
typedef uint16_t be16_t;

// domain socket to send ip packets
extern struct sockaddr_un remote_addr;
extern int socket_fd;
extern FILE *pcap_fp;

// configurable packet drop rate
// can be set in command line
extern double recv_drop_rate;
extern double send_drop_rate;

// configurable packet send delay in ms
extern double send_delay_min;
extern double send_delay_max;

// configurable http server index page
extern std::vector<std::string> http_index;

// configurable congestion control algorithm
enum CongestionControlAlgorithm {
  Default,
  NewReno,
  CUBIC,
  BBR,
};
extern CongestionControlAlgorithm current_cc_algo;

// set socket to blocking/non-blocking
bool set_socket_blocking(int fd, bool blocking);

// create sockaddr_un from path
struct sockaddr_un create_sockaddr_un(const char *path);

// setup unix socket
int setup_unix_socket(const char *path);

// setup tun device
int open_device(std::string tun_name);

// set local/remote ip
void set_ip(const char *local_ip, const char *remote_ip);

// taken from https://wiki.wireshark.org/Development/LibpcapFileFormat
typedef struct pcap_header_s {
  uint32_t magic_number;
  uint16_t version_major;
  uint16_t version_minor;
  uint32_t thiszone;
  uint32_t sigfigs;
  uint32_t snaplen;
  uint32_t network;
} pcap_header_t;

typedef struct pcap_packet_header_s {
  uint32_t tv_sec;
  uint32_t tv_usec;
  uint32_t caplen;
  uint32_t len;
} pcap_packet_header_t;

// create pcap file
FILE *pcap_create(const char *path);

// write packet to pcap file
void pcap_write(FILE *fp, const uint8_t *data, size_t size);

// send packet to remote
void send_packet(const uint8_t *data, size_t size);

// recv packet from remote
ssize_t recv_packet(uint8_t *buffer, size_t buffer_size);

void parse_argv(int argc, char *argv[]);

// ref: https://stackoverflow.com/a/3586005/2148614
// you can use these to colorize your output
#define RED "\x1B[31m"
#define GRN "\x1B[32m"
#define YEL "\x1B[33m"
#define BLU "\x1B[34m"
#define MAG "\x1B[35m"
#define CYN "\x1B[36m"
#define WHT "\x1B[37m"
#define RESET "\x1B[0m"

#endif