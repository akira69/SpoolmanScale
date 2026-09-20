#pragma once

#include <stddef.h>
#include "services/label_raster.h"

struct M220Device { char name[32]; char address[18]; };

constexpr uint16_t M220_DEFAULT_MEDIA_WIDTH_MM = 40;
constexpr uint16_t M220_DEFAULT_MEDIA_LENGTH_MM = 30;
constexpr uint16_t M220_BASE_RASTER_WIDTH = 576;
constexpr uint16_t M220_MAX_RASTER_WIDTH = 648;

inline uint16_t m220DotsForMm(uint16_t mm) {
  return (uint32_t(mm) * 2030 + 127) / 254;
}

inline uint16_t m220RasterWidthForMedia(uint16_t width_mm) {
  const uint16_t content = m220DotsForMm(width_mm);
  const uint16_t canvas = content > M220_BASE_RASTER_WIDTH ? content : M220_BASE_RASTER_WIDTH;
  return (canvas + 7) & ~uint16_t(7);
}

inline bool labelRasterFitsM220Media(const LabelRaster& image,
                                    uint16_t width_mm, uint16_t length_mm) {
  return image.content_width == m220DotsForMm(width_mm) &&
         image.height == m220DotsForMm(length_mm);
}
size_t phomemoM220Scan(M220Device* out, size_t capacity);
bool phomemoM220Print(const char* ble_address, const LabelRaster& image,
                     char* error, size_t error_size);
