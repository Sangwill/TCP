#include "buffer.h"
#include "tcp_header.h"
#include "timers.h"

static inline size_t min(size_t a, size_t b) { return a > b ? b : a; }


// ********** RingBuffer ********** //
template <size_t N>
RingBuffer<N>::RingBuffer() { begin = size = 0; }

// write data to ring buffer
template <size_t N>
size_t RingBuffer<N>::write(const uint8_t *data, size_t len) {
  size_t bytes_left = N - size;
  size_t bytes_written = min(bytes_left, len);
  // first part
  size_t part1_index = (begin + size) % N;
  size_t part1_size = min(N - part1_index, bytes_written);
  memcpy(&buffer[part1_index], data, part1_size);
  // second part if wrap around
  if (N - part1_index < bytes_written) {
    memcpy(&buffer[0], &data[part1_size], bytes_written - part1_size);
  }
  size += bytes_written;
  return bytes_written;
}

// read data from ring buffer
template <size_t N>
size_t RingBuffer<N>::read(uint8_t *data, size_t len) {
  size_t bytes_read = min(size, len);
  // first part
  size_t part1_size = min(N - begin, bytes_read);
  memcpy(data, &buffer[begin], part1_size);
  // second part if wrap around
  if (N - begin < bytes_read) {
    memcpy(&data[part1_size], buffer, bytes_read - part1_size);
  }
  size -= bytes_read;
  begin = (begin + bytes_read) % N;
  return bytes_read;
}

// allocate space in ring buffer
template <size_t N>
size_t RingBuffer<N>::alloc(size_t len) {
  size_t bytes_left = N - size;
  size_t bytes_allocated = min(bytes_left, len);
  size += bytes_allocated;
  return bytes_allocated;
}

// free data in ring buffer
template <size_t N>
size_t RingBuffer<N>::free(size_t len) {
  size_t bytes_freed = min(size, len);
  size -= bytes_freed;
  begin = (begin + bytes_freed) % N;
  return bytes_freed;
}

// return free bytes in ring buffer
template <size_t N>
size_t RingBuffer<N>::free_bytes() const { return N - size; }

// explicit instantiations
template class RingBuffer<10240>;

// ********** RecvRingBuffer ********** //
template <size_t N>
RecvRingBuffer<N>::RecvRingBuffer() : RingBuffer<N>() { 
  recv_size = 0;
  memset(recved, 0, N);
}

// write data to recv ring buffer
template <size_t N>
size_t RecvRingBuffer<N>::write(const uint8_t *data, size_t len, size_t offset) {
  size_t current = (RingBuffer<N>::begin + recv_size + offset) % N;
  size_t bytes_able_to_write = min(N - recv_size - offset, len);
  size_t bytes_written = 0;
  size_t old_recv_size = recv_size;
  for (size_t i = 0; i < bytes_able_to_write; i++) {
    if (!recved[current]) {
      recved[current] = true;
      bytes_written++;
    }
    RingBuffer<N>::buffer[current] = data[i];
    current = (current + 1) % N;
  }
  RingBuffer<N>::size += bytes_written;
  this->order();
  // return recv_wnd delta
  return recv_size - old_recv_size;
}

// read received data in ring buffer
template <size_t N>
size_t RecvRingBuffer<N>::read(uint8_t *data, size_t len) {
  size_t bytes_read = min(recv_size, len);
  // first part
  size_t part1_size = min(N - RingBuffer<N>::begin, bytes_read);
  memcpy(data, &RingBuffer<N>::buffer[RingBuffer<N>::begin], part1_size);
  memset(&recved[RingBuffer<N>::begin], 0, part1_size);
  // second part if wrap around
  if (N - RingBuffer<N>::begin < bytes_read) {
    memcpy(&data[part1_size], RingBuffer<N>::buffer, bytes_read - part1_size);
    memset(&recved[0], 0, bytes_read - part1_size);
  }
  recv_size -= bytes_read;
  RingBuffer<N>::size -= bytes_read;
  RingBuffer<N>::begin = (RingBuffer<N>::begin + bytes_read) % N;
  return bytes_read;
}

// order received data in ring buffer
template <size_t N>
void RecvRingBuffer<N>::order() {
  size_t current = (RingBuffer<N>::begin + recv_size) % N;
  while (recved[current] && recv_size < N) {
    recv_size++;
    current = (current + 1) % N;
  }
}

// free bytes in ring buffer
template <size_t N>
size_t RecvRingBuffer<N>::free_bytes() const {
  return N - recv_size;
}

// explicit instantiations
template class RecvRingBuffer<10240>;


// ********** SendRingBuffer ********** //
template <size_t N>
SendRingBuffer<N>::SendRingBuffer() : RingBuffer<N>() { 
  sent_size = 0;
}

// read data to send from ring buffer
template <size_t N>
size_t SendRingBuffer<N>::read(uint8_t *data, size_t len) {
  size_t bytes_read = min(RingBuffer<N>::size - sent_size, len);
  // first part
  size_t part1_index = (RingBuffer<N>::begin + sent_size) % N;
  size_t part1_size = min(N - part1_index, bytes_read);
  memcpy(data, &RingBuffer<N>::buffer[part1_index], part1_size);
  // second part if wrap around
  if (N - part1_index < bytes_read) {
    memcpy(&data[part1_size], RingBuffer<N>::buffer, bytes_read - part1_size);
  }
  sent_size += bytes_read;
  return bytes_read;
}

// free sent data in ring buffer
template <size_t N>
size_t SendRingBuffer<N>::free(size_t len) {
  size_t bytes_freed = min(sent_size, len);
  sent_size -= bytes_freed;
  RingBuffer<N>::size -= bytes_freed;
  RingBuffer<N>::begin = (RingBuffer<N>::begin + bytes_freed) % N;
  return bytes_freed;
}

// explicit instantiations
template class SendRingBuffer<10240>;