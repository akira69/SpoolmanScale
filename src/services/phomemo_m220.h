#pragma once

#include <stddef.h>
#include "services/label_raster.h"

struct M220Device { char name[32]; char address[18]; };
inline bool labelRasterFitsM220Media(const LabelRaster& image) {
  return image.content_width == 320 && image.height == 240;
}
size_t phomemoM220Scan(M220Device* out, size_t capacity);
bool phomemoM220Print(const char* ble_address, const LabelRaster& image,
                     char* error, size_t error_size);
