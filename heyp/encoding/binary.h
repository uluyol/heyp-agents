#ifndef HEYP_ENCODING_BINARY_H_
#define HEYP_ENCODING_BINARY_H_

#include <cstdint>

namespace heyp {

// Read an int32/64 from the buffer (little endian).

inline int32_t ReadI32LE(char *b) {
  return (b[3] << 24) | (b[2] << 16) | (b[1] << 8) | b[0];
}

inline int64_t ReadI64LE(char *b) {
  int64_t lo = ReadI32LE(b);
  int64_t hi = ReadI32LE(b + 4);
  return (hi << 32) | lo;
}

// Write an int32/64 to the buffer (little endian).

inline void WriteI32LE(int32_t v, char *b) {
  b[0] = v & 0xff;
  b[1] = (v >> 8) & 0xff;
  b[2] = (v >> 16) & 0xff;
  b[3] = (v >> 24) & 0xff;
}

inline void WriteI64LE(int64_t v, char *b) {
  WriteI32LE(v, b);
  WriteI32LE(v >> 32, b);
}

}  // namespace heyp

#endif  //  HEYP_ENCODING_BINARY_H_
