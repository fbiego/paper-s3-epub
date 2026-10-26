#pragma once
#include <cstddef>
#include <cstdint>

namespace bletransfer {
inline uint32_t offset(const uint8_t* data) {
  return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}
inline void putOffset(uint8_t* data, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) data[i] = uint8_t(value >> (8 * i));
}
inline uint32_t updateCrc(uint32_t crc, const uint8_t* data, size_t count) {
  while (count--) {
    crc ^= *data++;
    for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return crc;
}
inline bool fits(uint32_t offset, size_t length, uint32_t expected) {
  return offset <= expected && length <= expected - offset;
}
} // namespace bletransfer
