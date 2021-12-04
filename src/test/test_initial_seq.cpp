#include <assert.h>
#include <set>

#include "tcp.h"

int main(int argc, char *argv[]) {
  int N = 1000;
  std::set<uint32_t> numbers;
  for (int i = 0; i < 1000; i++) {
    uint32_t seq = generate_initial_seq();
    numbers.insert(seq);
    usleep(10);
  }
  assert(numbers.size() == N);
  return 0;
}