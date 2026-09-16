#pragma once

#include "services/label_raster.h"

int filamanFetchMonoLabel(const char* base_url, const char* api_key, int spool_id,
                          int preset_id, uint16_t requested_width, LabelRaster* out,
                          uint32_t timeout_ms = 12000);
void filamanFreeLabel(LabelRaster* image);
