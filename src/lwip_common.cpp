#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>

#include "common.h"
#include "lwip/arch.h"
#include "lwip/timeouts.h"
#include "lwip_common.h"

// support functions for lwip
extern "C" {

u32_t sys_jiffies(void) { return 0; }

u32_t sys_now() {
  struct timeval tv = {};
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
}

err_t netif_output(struct netif *netif, struct pbuf *p,
                   const ip4_addr_t *ipaddr) {
  // p might have next, don't use payload directly
  u8_t *buffer = (u8_t *)malloc(p->tot_len);
  pbuf_copy_partial(p, buffer, p->tot_len, 0);

  send_packet(buffer, p->tot_len);

  free(buffer);
  return ERR_OK;
}

err_t netif_init(struct netif *netif) {
  netif->output = netif_output;
  netif->mtu = 1500;
  netif->flags = 0;
  return ERR_OK;
}

ip_addr_t ip4_from_string(const char *addr) {
  ip_addr_t ip_addr = {0};
  ip4addr_aton(addr, &ip_addr);
  return ip_addr;
}

void loop_yield() {
  uint32_t time = sys_timeouts_sleeptime();
  if (time > 50) {
    time = 50;
  }
  usleep(time * 1000);
}