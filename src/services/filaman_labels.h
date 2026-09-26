#pragma once

#include "services/label_raster.h"

// Local fetch errors surfaced as specific guidance by the scale UI.
constexpr int FILAMAN_LABEL_NO_PSRAM = -1001;
constexpr int FILAMAN_LABEL_PRESET_REQUIRES_CHROMIUM = -1002;

// Allow the server's 30-second render deadline plus transfer overhead.
int filamanFetchMonoLabel(const char* base_url, const char* api_key, int spool_id,
                          int preset_id, uint16_t requested_width, const char* orientation,
                          LabelRaster* out, int* resolved_preset_id = nullptr,
                          uint32_t timeout_ms = 35000);
void filamanFreeLabel(LabelRaster* image);
