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
#include "lwip/apps/http_client.h"
#include "lwip/arch.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/opt.h"
#include "lwip/timeouts.h"
#include "lwip_common.h"

// callbacks
err_t httpc_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {

  printf("HTTP Got Body:\n");

  // p might have next, don't use payload directly
  u8_t *buffer = (u8_t *)malloc(p->tot_len);
  pbuf_copy_partial(p, buffer, p->tot_len, 0);
  fwrite(buffer, p->tot_len, 1, stdout);

  free(buffer);
  return ERR_OK;
}

void httpc_result(void *arg, httpc_result_t httpc_result, u32_t rx_content_len,
                  u32_t srv_res, err_t err) {
  printf("HTTP Result: %d\n", httpc_result);
}

int main(int argc, char *argv[]) {
  parse_argv(argc, argv);

  // init lwip
  struct netif netif;
  lwip_init();
  ip_addr_t client_addr = ip4_from_string("10.0.0.2");
  ip_addr_t netmask = ip4_from_string("255.255.255.0");
  netif_add(&netif, &client_addr, &netmask, IP4_ADDR_ANY, NULL, netif_init,
            netif_input);
  netif.name[0] = 'e';
  netif.name[1] = '0';
  netif_set_default(&netif);
  netif_set_up(&netif);
  netif_set_link_up(&netif);

  ip_addr_t server_addr = ip4_from_string("10.0.0.1");
  httpc_connection_t settings{0};
  settings.result_fn = httpc_result;
  httpc_get_file(&server_addr, 80, "/index.html", &settings, httpc_recv, NULL,
                 NULL);

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
