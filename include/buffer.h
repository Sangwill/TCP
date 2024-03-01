#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <stdexcept>

template <size_t N> struct RingBuffer {
  // ring buffer from [begin, begin+size)
  uint8_t buffer[N];
  size_t begin;
  size_t size;

  RingBuffer();

  // write data to ring buffer
  size_t write(const uint8_t *data, size_t len);

  // read data from ring buffer
  size_t read(uint8_t *data, size_t len);

  // allocate space in ring buffer
  size_t alloc(size_t len);

  // free data in ring buffer
  size_t free(size_t len);

  // return free bytes in ring buffer
  size_t free_bytes() const;
};

template <size_t N> struct RecvRingBuffer : public RingBuffer<N> {
  // recv ring buffer from [begin, begin+size)

  size_t recv_size;
  bool recved[N];

  RecvRingBuffer();
  
  // write data to recv ring buffer
  size_t write(const uint8_t *data, size_t len, size_t offset);

  // read received data in ring buffer
  size_t read(uint8_t *data, size_t len);

  // order received data in ring buffer
  void order();

  // free bytes in ring buffer
  size_t free_bytes() const;
};

template <size_t N> struct SendRingBuffer : public RingBuffer<N> {
  // send ring buffer from [begin, begin+size)

  size_t sent_size;

  SendRingBuffer();

  // read data to send from ring buffer
  size_t read(uint8_t *data, size_t len);

  // free sent data in ring buffer
  size_t free(size_t len);
};

#endif /* __BUFFER_H__ */
