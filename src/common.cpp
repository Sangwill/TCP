#include <assert.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>

#ifdef __APPLE__

#include <net/if.h>
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
#include "tcp_header.h"

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
const char *local_ip = "";
const char *remote_ip = "";

// random packet drop
// never drop by default
double recv_drop_rate = 0.0;
double send_drop_rate = 0.0;

// random delay in ms
double send_delay_min = 0.0;
double send_delay_max = 0.0;

std::vector<std::string> http_index;

// default congestion control algorithm
CongestionControlAlgorithm current_cc_algo =
    CongestionControlAlgorithm::Default;

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

#include <map>

// send packet drop control
#define MIN_DROP_TIMES 1
#define MAX_DROP_TIMES 3
std::map<uint32_t, uint32_t> packet_drop_count;

void send_packet_internal(const uint8_t *data, size_t size) {
  printf("TX:");
  for (size_t i = 0; i < size; i++) {
    printf(" %02X", data[i]);
  }
  printf("\n");
  if (send_drop_rate) {
    // drop packet at least MIN_DROP_TIMES
    uint32_t seq = ntohl(((TCPHeader*)(data + 20))->seq);
    // simply regard seq as the identifier of a packet
    if (packet_drop_count[seq] < MIN_DROP_TIMES) {
      packet_drop_count[seq]++;
      printf("Send packet dropped\n");
      return;
    }
    // drop packet at most MAX_DROP_TIMES with probability send_drop_rate
    if (packet_drop_count[seq] < MAX_DROP_TIMES && (double)rand() / RAND_MAX < send_drop_rate) {
      packet_drop_count[seq]++;
      printf("Send packet dropped\n");
      return;
    }
  }
  // save to pcap
  if (pcap_fp) {
    pcap_write(pcap_fp, data, size);
  }

  // send to remote
  if (io_config == UnixSocket) {
    while(sendto(socket_fd, data, size, 0, (struct sockaddr *)&remote_addr,
           sizeof(remote_addr)) < 0);
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

struct delay_sender {
  std::vector<uint8_t> data;

  int operator()() {
    send_packet_internal(data.data(), data.size());
    return -1;
  }
};

const int DELAY_SLOT_COUNT = 10;
double delay_slots[DELAY_SLOT_COUNT] = {0};

void update_delay_slots() {
  for (int i = 0; i < DELAY_SLOT_COUNT; i++) {
    delay_slots[i] = send_delay_min + ((double)rand() / RAND_MAX) *
                                          (send_delay_max - send_delay_min);
  }
  std::sort(delay_slots, delay_slots + DELAY_SLOT_COUNT, std::greater<double>());
}

double get_delay_slot() {
  static int index = DELAY_SLOT_COUNT;
  if (index == DELAY_SLOT_COUNT) {
    index = 0;
    update_delay_slots();
  }
  return delay_slots[index++];
}

void send_packet(const uint8_t *data, size_t size) {
  if (send_delay_max != 0.0) {
    // simulate transfer latency
    double time = get_delay_slot();
    printf("Delay in %.2lf ms\n", time);

    delay_sender fn;
    fn.data.insert(fn.data.begin(), data, data + size);
    TIMERS.schedule_job(fn, time);
  } else {
    send_packet_internal(data, size);
  }
}


// send packet throughput control
#define MAX_TPS (10 * 1024)

enum TPSControlMode {
  None,
  Fixed,
  Random,
  Continue,
};
TPSControlMode tps_control_mode = TPSControlMode::None;

ssize_t recv_packet(uint8_t *buffer, size_t buffer_size) {
  static int bytes_recv = 0;
  static uint64_t first_packet_time = current_ts_msec();
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
    uint64_t now = current_ts_msec();
    // tps_control_handle() returns true if the packet should be dropped
    // otherwise, the packet should be passed
    static auto tps_control_handle = [&]() {
      switch (tps_control_mode) {
        case TPSControlMode::Fixed:
          // drop packet if the throughput is higher than MAX_TPS
          return (bytes_recv + size) * 1000 > (now - first_packet_time) * MAX_TPS;
        case TPSControlMode::Random:
          // drop packet with probability 0.1 if the throughput is higher than MAX_TPS
          return (bytes_recv + size) * 1000 > (now - first_packet_time) * MAX_TPS &&
                 (double)rand() / RAND_MAX < 0.1;
        case TPSControlMode::Continue: {
          // drop 3 packets for every 20 packets passed
          static size_t drop_count = 3;
          static size_t pass_count = 0;
          if (size > 1000) {
            // only count packets larger than 1000 bytes
            if (pass_count) {
              pass_count--;
              return false;
            } else {
              if (drop_count) {
                drop_count--;
                return true;
              } else {
                // reset drop_count and pass_count
                drop_count = 3;
                pass_count = 20;
                return false;
              }
            }
          }
          return false;
        }
        default:
          return false;
      }
    };
    if (tps_control_handle()) {
      printf("Recv packet dropped for throughput control at time %lu ms\n", now - first_packet_time);
      return -1;
    }
    bytes_recv += size;
    // print
    printf("RX:");
    for (ssize_t i = 0; i < size; i++) {
      printf(" %02X", buffer[i]);
    }
    printf("\n");

    if (recv_drop_rate) {
      // drop packet at least MIN_DROP_TIMES
      uint32_t seq = ntohl(((TCPHeader*)(buffer + 20))->seq);
      if (packet_drop_count[seq] < MIN_DROP_TIMES) {
        packet_drop_count[seq]++;
        printf("Recv packet dropped\n");
        return -1;
      }

      if (packet_drop_count[seq] < MAX_DROP_TIMES && (double)rand() / RAND_MAX < recv_drop_rate) {
        packet_drop_count[seq]++;
        printf("Recv packet dropped, drop times %d\n", packet_drop_count[seq]);
        return -1;
      }
    }

    // pcap
    if (pcap_fp) {
      pcap_write(pcap_fp, buffer, size);
    }
  }
  return size;
}

void set_congestion_control_algo(const char *algo) {
  std::string s = algo;
  if (s == "Default") {
    current_cc_algo = CongestionControlAlgorithm::Default;
    printf("Congestion Control Algo is Default\n");
  } else if (s == "NewReno") {
    current_cc_algo = CongestionControlAlgorithm::NewReno;
    printf("Congestion Control Algo is NewReno\n");
  } else if (s == "CUBIC") {
    current_cc_algo = CongestionControlAlgorithm::CUBIC;
    printf("Congestion Control Algo is CUBIC\n");
  } else if (s == "BBR") {
    current_cc_algo = CongestionControlAlgorithm::BBR;
    printf("Congestion Control Algo is BBR\n");
  }
}

void generate_http_index(const char *test_case=nullptr) {
  static const char *index_data[5] = {
    "HTTP/1.1 200 OK\r\n",
    "Content-Length: 13\r\n",
    "Content-Type: text/plain; charset=utf-8\r\n",
    "\r\n",
    "Hello World!\n",
  };
  http_index.clear();
  auto hello_world_multi_lines = [&](int times) {
    for (int i = 0; i < 4; i++) {
      http_index.push_back(std::string(index_data[i]));
    }
    http_index[1] = std::string("Content-Length: ") + std::to_string(times * 13) + std::string("\r\n");
    for (int i = 0; i < times; i++) {
      http_index.push_back(std::string(index_data[4]));
    }
  };
  auto hello_world_one_line = [&](int times) {
    for (int i = 0; i < 4; i++) {
      http_index.push_back(std::string(index_data[i]));
    }
    http_index[1] = std::string("Content-Length: ") + std::to_string(times * 13) + std::string("\r\n");
    // repeat hello world for times
    std::string one_line = "";
    for (int i = 0; i < times; i++) {
      one_line += std::string(index_data[4]);
    }
    http_index.push_back(one_line);
  };
  if (!test_case) {
    hello_world_multi_lines(1);
    return;
  }
  if (strcmp(test_case, "nagle") == 0) {
    hello_world_multi_lines(100);
    return;
  }
  if (strcmp(test_case, "out-of-order") == 0) {
    hello_world_multi_lines(5);
    return;
  }
  if (strcmp(test_case, "cong-avoid-client") == 0) {
    tps_control_mode = TPSControlMode::Fixed;
    return;
  }
  if (strcmp(test_case, "cong-avoid-client-2") == 0) {
    tps_control_mode = TPSControlMode::Random;
    return;
  }
  if (strcmp(test_case, "new-reno") == 0) {
    tps_control_mode = TPSControlMode::Continue;
    return;
  }
  if (strcmp(test_case, "cong-avoid-server") == 0) {
    hello_world_one_line(10000);
    return;
  }
  throw std::runtime_error("Unknown test case");
}

void parse_argv(int argc, char *argv[]) {
  int c;
  int hflag = 0;
  char *local = NULL;
  char *remote = NULL;
  char *pcap = NULL;
  char *tun = NULL;
  srand(time(NULL));

  // parse arguments
  while ((c = getopt(argc, argv, "hl:r:t:p:R:S:c:s:T:")) != -1) {
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
    case 'R':
      sscanf(optarg, "%lf", &recv_drop_rate);
      printf("Recv drop rate is now %lf\n", recv_drop_rate);
      break;
    case 'S':
      sscanf(optarg, "%lf", &send_drop_rate);
      printf("Send drop rate is now %lf\n", send_drop_rate);
      break;
    case 'c':
      set_congestion_control_algo(optarg);
      break;
    case 's':
      if (strchr(optarg, ',') != NULL) {
        // min, max
        sscanf(optarg, "%lf,%lf", &send_delay_min, &send_delay_max);
        assert(send_delay_min <= send_delay_max);
      } else {
        // min = max
        sscanf(optarg, "%lf", &send_delay_min);
        send_delay_max = send_delay_min;
      }
      printf("Send delay is [%lf,%lf] ms\n", send_delay_min, send_delay_max);
      break;
    case 'T':
      generate_http_index(optarg);
      break;
    case '?':
      fprintf(stderr, "Unknown option: %c\n", optopt);
      exit(1);
      break;
    default:
      break;
    }
  }

  if (!http_index.size()) {
    generate_http_index();
  }

  if (hflag) {
    fprintf(stderr,
            "Usage: %s [-h] [-l LOCAL] [-r REMOTE] [-t TUN] [-p PCAP] [-R "
            "FLOAT] [-S FLOAT] [-c ALGO] [-s DELAY[MIN,MAX]] [-T TEST_CASE]\n",
            argv[0]);
    fprintf(stderr, "\t-l LOCAL: local unix socket path\n");
    fprintf(stderr, "\t-r REMOTE: remote unix socket path\n");
    fprintf(stderr, "\t-t TUN: use tun interface\n");
    fprintf(stderr, "\t-p PCAP: pcap file for debugging\n");
    fprintf(stderr, "\t-R FLOAT: recv packet drop rate\n");
    fprintf(stderr, "\t-S FLOAT: send packet drop rate\n");
    fprintf(stderr, "\t-c ALGO: congestion control algorithm: Default, "
                    "NewReno, CUBIC, BBR\n");
    fprintf(
        stderr,
        "\t-s DELAY[MIN,MAX]: add random delay time(ms) in sending packets\n");
    fprintf(stderr, "\t-T TEST_CASE: test case: nagle, out-of-order, "
                    "cong-avoid-client, cong-avoid-client-2, cong-avoid-server, "
                    "new-reno\n");
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
    printf("Saving packets to %s\n", pcap);
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
  printf("Running: %s\n", command.c_str());
  system(command.c_str());

  // from macOS's perspective
  // local/remote is reversed
  command = std::string("ifconfig ") + "'" + tun_name + "' " + remote_ip + " " +
            local_ip;
  printf("Running: %s\n", command.c_str());
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
    printf("Running: %s\n", command.c_str());
    system(command.c_str());

    // from linux's perspective
    // local/remote is reversed
    command = std::string("ip addr add ") + remote_ip + " peer " + local_ip +
              " dev '" + tun_name + "'";
    printf("Running: %s\n", command.c_str());
    system(command.c_str());
  }

#endif

  printf("Device %s is now up\n", tun_name.c_str());
  io_config = IOConfig::TUN;
  set_socket_blocking(fd, false);
  tun_fd = fd;
  return fd;
}

void set_ip(const char *new_local_ip, const char *new_remote_ip) {
  printf("Local IP is %s\n", new_local_ip);
  printf("Remote IP is %s\n", new_remote_ip);
  local_ip = new_local_ip;
  remote_ip = new_remote_ip;
}