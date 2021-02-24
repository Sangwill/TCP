#ifndef __LWIP_COMMON_H__
#define __LWIP_COMMON_H__

#include "lwip/netif.h"

// netif output handler
err_t netif_output(struct netif *netif, struct pbuf *p,
                   const ip4_addr_t *ipaddr);

// setup netif
err_t netif_init(struct netif *netif);

// create ip_addr_t from string
ip_addr_t ip4_from_string(const char *addr);

// yield until next timeout or 50ms
void loop_yield();

#endif