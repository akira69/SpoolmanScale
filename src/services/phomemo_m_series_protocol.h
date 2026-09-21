#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>

constexpr uint16_t PHOMEMO_PREFERRED_MTU = 185;

inline std::array<uint8_t, 8> phomemoRasterHeader(uint16_t width, uint16_t height) {
  const uint16_t bytes = (width + 7) / 8;
  return {0x1d, 0x76, 0x30, 0x00, uint8_t(bytes), uint8_t(bytes >> 8),
          uint8_t(height), uint8_t(height >> 8)};
}

inline size_t phomemoWriteChunk(uint16_t mtu) {
  return mtu > 3 ? (mtu - 3 < 128 ? mtu - 3 : 128) : 20;
}

inline std::array<uint8_t, 4> m110SpeedCommand(uint8_t speed) {
  return {0x1b, 0x4e, 0x0d, speed};
}

inline std::array<uint8_t, 4> m110DensityCommand(uint8_t density) {
  return {0x1b, 0x4e, 0x04, density};
}

inline std::array<uint8_t, 3> m110MediaCommand(uint8_t media) {
  return {0x1f, 0x11, media};
}

inline std::array<uint8_t, 4> m110FooterStart() {
  return {0x1f, 0xf0, 0x05, 0x00};
}

inline std::array<uint8_t, 4> m110FooterEnd() {
  return {0x1f, 0xf0, 0x03, 0x00};
}
