#include <assert.h>
#include <set>

#include "tcp.h"

int main(int argc, char *argv[]) {
  assert(tcp_seq_lt(0x0000, 0x0001));
  assert(tcp_seq_lt(0xFFFF, 0x0000));
  assert(tcp_seq_lt(0x1000, 0x2000));
  assert(tcp_seq_lt(0xF000, 0xFFFF));

  assert(tcp_seq_le(0x0000, 0x0001));
  assert(tcp_seq_le(0x0000, 0x0000));
  assert(tcp_seq_le(0xF000, 0xFFFF));
  assert(tcp_seq_le(0xFFFF, 0x0000));
  assert(tcp_seq_le(0xF000, 0x0000));

  assert(tcp_seq_gt(0x0001, 0x0000));
  assert(tcp_seq_gt(0x0000, 0xFFFF));

  assert(tcp_seq_ge(0x1234, 0x0000));
  assert(tcp_seq_ge(0xFFFF, 0xF000));

  return 0;
}