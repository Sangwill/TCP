#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <net/if.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__

#include <net/if_utun.h>
#include <sys/kern_control.h>
#include <sys/kern_event.h>
#include <sys/sys_domain.h>

#else

#include <linux/if.h>
#include <linux/if_tun.h>

#endif

#include "common.h"
#include "ip.h"
#include "timers.h"

// global state
struct sockaddr_un remote_addr;
enum IOConfig {
  UnixSocket,
  TUN,
} io_config;
int socket_fd = 0;
int tun_fd = 0;
FILE *pcap_fp = nullptr;
int tun_padding_len = 0;
const uint32_t tun_padding_v4 = htonl(AF_INET);
const uint32_t tun_padding_v6 = htonl(AF_INET6);
const char *local_ip = "";
const char *remote_ip = "";

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
  if (io_config == UnixSocket) {
    sendto(socket_fd, data, size, 0, (struct sockaddr *)&remote_addr,
           sizeof(remote_addr));
  } else {
    // prepend padding if needed
    const uint8_t *ptr = data;
    uint8_t buffer[4096];
    if (tun_padding_len > 0) {
      memcpy(buffer + tun_padding_len, data, size);
      memcpy(buffer, &tun_padding_v4, tun_padding_len);
      ptr = buffer;
      size += tun_padding_len;
    }

    write(tun_fd, ptr, size);
  }
}

ssize_t recv_packet(uint8_t *buffer, size_t buffer_size) {
  ssize_t size = 0;
  if (io_config == IOConfig::UnixSocket) {
    struct sockaddr_un addr = {};
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);
    size = recvfrom(socket_fd, buffer, buffer_size, 0, (struct sockaddr *)&addr,
                    &len);
  } else {
    size = read(tun_fd, buffer, buffer_size);

    // remove padding if needed
    if (tun_padding_len > 0) {
      if (size < tun_padding_len) {
        return -1;
      }
      size -= tun_padding_len;
      memmove(buffer, buffer + tun_padding_len, size);
    }
  }

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
  char *tun = NULL;

  // parse arguments
  while ((c = getopt(argc, argv, "hl:r:t:p:")) != -1) {
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
    case 't':
      tun = optarg;
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
    fprintf(stderr, "\t-t TUN: use tun interface\n");
    fprintf(stderr, "\t-p PCAP: pcap file for debugging\n");
    exit(0);
  }

  if (tun) {
    printf("Using TUN interface: %s\n", tun);
    open_device(tun);
  } else {
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
    io_config = IOConfig::UnixSocket;
  }

  if (pcap) {
    pcap_fp = pcap_create(pcap);
  }

  // init random
  srand(current_ts_msec());
}

int open_device(std::string tun_name) {
  int fd = -1;

#ifdef __APPLE__
  if (tun_name.find("utun") == 0 || tun_name == "") {
    // utun
    fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0) {
      perror("failed to create socket");
      exit(1);
    }

    struct ctl_info info;
    bzero(&info, sizeof(info));
    strncpy(info.ctl_name, UTUN_CONTROL_NAME, strlen(UTUN_CONTROL_NAME));

    if (ioctl(fd, CTLIOCGINFO, &info) < 0) {
      perror("failed to ioctl on tun device");
      close(fd);
      exit(1);
    }

    struct sockaddr_ctl ctl;
    ctl.sc_id = info.ctl_id;
    ctl.sc_len = sizeof(ctl);
    ctl.sc_family = AF_SYSTEM;
    ctl.ss_sysaddr = AF_SYS_CONTROL;
    ctl.sc_unit = 0;

    if (tun_name.find("utun") == 0 && tun_name.length() > strlen("utun")) {
      // user specified number
      ctl.sc_unit =
          stoi(tun_name.substr(tun_name.find("utun") + strlen("utun"))) + 1;
    }

    if (connect(fd, (struct sockaddr *)&ctl, sizeof(ctl)) < 0) {
      perror("failed to connect to tun");
      close(fd);
      exit(1);
    }

    char ifname[IFNAMSIZ];
    socklen_t ifname_len = sizeof(ifname);

    if (getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, ifname, &ifname_len) <
        0) {
      perror("failed to getsockopt for tun");
      close(fd);
      exit(1);
    }
    tun_name = ifname;
    // utun has a 32-bit loopback header ahead of data
    tun_padding_len = sizeof(uint32_t);
  } else {
    fprintf(stderr, "Bad tunnel name %s\n", tun_name.c_str());
    exit(1);
  }

  std::string command = "ifconfig '" + tun_name + "' up";
  system(command.c_str());

  // from macOS's perspective
  // local/remote is reversed
  command = std::string("ifconfig ") + "'" + tun_name + "' " + remote_ip + " " +
            local_ip;
  system(command.c_str());
#else

  struct ifreq ifr = {};
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  strncpy(ifr.ifr_name, tun_name.c_str(), IFNAMSIZ);

  fd = open("/dev/net/tun", O_RDWR);
  if (fd < 0) {
    perror("failed to open /dev/net/tun");
    exit(1);
  } else {
    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
      perror("fail to set tun ioctl");
      close(fd);
      exit(1);
    }
    tun_name = ifr.ifr_name;

    std::string command = "ip link set dev '" + tun_name + "' up";
    system(command.c_str());

    // from linux's perspective
    // local/remote is reversed
    command = std::string("ip addr add ") + remote_ip + " peer " + local_ip +
              " dev '" + tun_name + "'";
    system(command.c_str());
  }

#endif

  printf("Device %s is now up.\n", tun_name.c_str());
  io_config = IOConfig::TUN;
  tun_fd = fd;
  return fd;
}

void set_ip(const char *new_local_ip, const char *new_remote_ip) {
  printf("Local IP is %s\n", new_local_ip);
  printf("Remote IP is %s\n", new_remote_ip);
  local_ip = new_local_ip;
  remote_ip = new_remote_ip;
}