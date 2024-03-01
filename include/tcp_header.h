#ifndef __TCP_HEADER_H__
#define __TCP_HEADER_H__

#include "common.h"

// default MSS is MTU - 20 - 20 for TCP over IPv4
#define DEFAULT_MSS (MTU - 20 - 20)

// taken from linux source include/uapi/linux/tcp.h
// RFC793 Page 15
struct TCPHeader {
  be16_t source;
  be16_t dest;
  be32_t seq;
  be32_t ack_seq;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  unsigned int res1 : 4;
  unsigned int doff : 4;
  unsigned int fin : 1;
  unsigned int syn : 1;
  unsigned int rst : 1;
  unsigned int psh : 1;
  unsigned int ack : 1;
  unsigned int urg : 1;
  unsigned int ece : 1;
  unsigned int cwr : 1;
#endif
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  unsigned int doff : 4;
  unsigned int res1 : 4;
  unsigned int cwr : 1;
  unsigned int ece : 1;
  unsigned int urg : 1;
  unsigned int ack : 1;
  unsigned int psh : 1;
  unsigned int rst : 1;
  unsigned int syn : 1;
  unsigned int fin : 1;
#endif
  be16_t window;
  be16_t checksum;
  be16_t urg_ptr;
};

#endif /* __TCP_HEADER_H__ */
