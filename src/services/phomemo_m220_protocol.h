#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>

inline std::array<uint8_t, 8> m220RasterHeader(uint16_t width, uint16_t height) {
  const uint16_t bytes = (width + 7) / 8;
  return {0x1d, 0x76, 0x30, 0x00, uint8_t(bytes), uint8_t(bytes >> 8),
          uint8_t(height), uint8_t(height >> 8)};
}

inline size_t m220WriteChunk(uint16_t mtu) {
  return mtu > 3 ? (mtu - 3 < 128 ? mtu - 3 : 128) : 20;
}
