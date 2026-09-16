#pragma once

#include <stddef.h>
#include <stdint.h>

struct LabelRaster {
  uint16_t width, height, row_bytes;
  uint8_t* pixels;
  size_t length;
  uint16_t content_width;
  bool rotated;
};

inline bool labelRasterPreviewBlack(const LabelRaster& image, uint16_t x, uint16_t y) {
  const uint16_t source_x = image.width - image.content_width + (image.rotated ? y : x);
  const uint16_t source_y = image.rotated ? image.height - 1 - x : y;
  return image.pixels[size_t(source_y) * image.row_bytes + source_x / 8] & (0x80 >> (source_x % 8));
}

inline bool labelRasterShapeValid(uint16_t width, uint16_t height,
                                  uint16_t row_bytes, size_t length) {
  return width >= 384 && width <= 1024 && height > 0 &&
         row_bytes == (width + 7) / 8 &&
         length <= 256 * 1024 && length == size_t(row_bytes) * height;
}

inline bool labelRasterPaddingValid(const LabelRaster& image) {
  if (!image.pixels || !labelRasterShapeValid(image.width, image.height,
                                              image.row_bytes, image.length)) return false;
  const unsigned unused = (8 - image.width % 8) % 8;
  if (!unused) return true;
  const uint8_t mask = (1u << unused) - 1;
  for (uint16_t y = 0; y < image.height; ++y)
    if (image.pixels[size_t(y + 1) * image.row_bytes - 1] & mask) return false;
  return true;
}
