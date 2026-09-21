#pragma once

#include <stddef.h>
#include <stdint.h>

#include "services/label_raster.h"

enum class LabelPrinterModel : uint8_t { NONE = 0, M220 = 1, M110 = 2 };

struct LabelPrinterProfile {
  LabelPrinterModel model;
  const char* name;
  uint16_t default_width_mm, default_length_mm;
  uint16_t min_width_mm, max_width_mm;
  uint16_t min_length_mm, max_length_mm;
  uint16_t base_raster_width, max_raster_width;
  bool experimental;
};

struct LabelPrinterConfig {
  LabelPrinterModel model;
  char name[32];
  char address[18];
  uint16_t media_width_mm, media_length_mm;
};

struct LabelPrinterDevice { char name[32]; char address[18]; };
using LabelPrinterProgressFn = void (*)();

constexpr char LABEL_PRINTER_BLE_START_CRUMB[] = "label printer BLE start";

size_t labelPrinterScan(const LabelPrinterConfig& selected,
                       LabelPrinterDevice* out, size_t capacity,
                       LabelPrinterProgressFn progress = nullptr);
bool labelPrinterPrint(const LabelPrinterConfig& config, const LabelRaster& image,
                       char* error, size_t error_size,
                       LabelPrinterProgressFn progress = nullptr);

const LabelPrinterProfile& labelPrinterProfile(LabelPrinterModel model);
LabelPrinterConfig labelPrinterLoadConfig();
bool labelPrinterSaveConfig(const LabelPrinterConfig& config);
bool labelPrinterConfigured(const LabelPrinterConfig& config);
bool labelPrinterReachable();
void labelPrinterSetReachable(bool reachable);
#ifdef UNIT_TEST
void labelPrinterResetConfigCache();
#endif
bool labelPrinterStartupCrash(const char* previous_crumb, bool panic_reset);
uint16_t labelPrinterDotsForMm(uint16_t mm);
uint16_t labelPrinterRasterWidth(LabelPrinterModel model, uint16_t media_width_mm);
bool labelPrinterRasterFits(LabelPrinterModel model, const LabelRaster& image,
                            uint16_t media_width_mm, uint16_t media_length_mm);
uint8_t labelPrinterDevicePriority(const LabelPrinterDevice& device,
                                   const LabelPrinterConfig& selected);
void labelPrinterConsiderDevice(LabelPrinterDevice* devices, size_t* count,
                                size_t capacity,
                                const LabelPrinterDevice& candidate,
                                const LabelPrinterConfig& selected);
void labelPrinterSortDevices(LabelPrinterDevice* devices, size_t count,
                             const LabelPrinterConfig& selected);
