#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "common.h"
#include "lwip/apps/httpd.h"
#include "lwip/arch.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/opt.h"
#include "lwip/timeouts.h"
#include "lwip_common.h"

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

  // init lwip
  struct netif netif;
  lwip_init();
  ip_addr_t server_addr = ip4_from_string("10.0.0.1");
  ip_addr_t netmask = ip4_from_string("255.255.255.0");
  netif_add(&netif, &server_addr, &netmask, IP4_ADDR_ANY, NULL, netif_init,
            netif_input);
  netif.name[0] = 'e';
  netif.name[1] = '0';
  netif_set_default(&netif);
  netif_set_up(&netif);
  netif_set_link_up(&netif);
  httpd_init();

  const size_t buffer_size = 1500;
  u8_t buffer[buffer_size];
  while (1) {
    ssize_t size = recv_packet(buffer, buffer_size);
    if (size >= 0) {
      // got data
      struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_RAM);
      memcpy(p->payload, buffer, size);

      if (netif.input(p, &netif) != ERR_OK) {
        pbuf_free(p);
      }
    }
    sys_check_timeouts();
    loop_yield();
  }
  return 0;
}
