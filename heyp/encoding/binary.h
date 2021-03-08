#ifndef HEYP_ENCODING_BINARY_H_
#define HEYP_ENCODING_BINARY_H_

#include <cstdint>
#include <string>

namespace heyp {

// Read an int32/64 from the buffer (little endian).

inline uint32_t ReadU32LE(const char *b) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(b[3])) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(b[2])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(b[1])) << 8) |
         static_cast<uint32_t>(static_cast<uint8_t>(b[0]));
}

inline uint64_t ReadU64LE(const char *b) {
  uint64_t lo = ReadU32LE(b);
  uint64_t hi = ReadU32LE(b + 4);
  return (hi << 32) | lo;
}

inline int32_t ReadI32LE(const char *b) { return ReadU32LE(b); }
inline int64_t ReadI64LE(const char *b) { return ReadU64LE(b); }

// Write an int32/64 to the buffer (little endian).

inline void WriteU32LE(uint32_t v, char *b) {
  b[0] = v & 0xff;
  b[1] = (v >> 8) & 0xff;
  b[2] = (v >> 16) & 0xff;
  b[3] = (v >> 24) & 0xff;
}

inline void WriteU64LE(uint64_t v, char *b) {
  WriteU32LE(v, b);
  WriteU32LE(v >> 32, b + 4);
}

inline void WriteI32LE(int32_t v, char *b) { WriteU32LE(v, b); }
inline void WriteI64LE(int64_t v, char *b) { WriteU64LE(v, b); }

std::string ToHex(char *b, int n) {
  std::string out;
  out.reserve(n * 2);

  auto to_char = [](uint8_t c) -> char {
    if (c < 10) {
      return '0' + c;
    }
    if (c < 16) {
      return 'a' + c - 10;
    }
    return '?';
  };

  while (n > 0) {
    uint8_t v = *b;
    uint8_t lo = v & 0xf;
    uint8_t hi = (v >> 4) & 0xf;
    out.push_back(to_char(lo));
    out.push_back(to_char(hi));
    ++b;
    --n;
  }
  return out;
}

}  // namespace heyp

#endif  //  HEYP_ENCODING_BINARY_H_
