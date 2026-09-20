#pragma once

#include "services/label_printer.h"

size_t phomemoMSeriesScan(LabelPrinterDevice* out, size_t capacity,
                        const LabelPrinterConfig& selected,
                        LabelPrinterProgressFn progress = nullptr);
bool phomemoMSeriesPrint(LabelPrinterModel model, const char* ble_address,
                        const LabelRaster& image, char* error, size_t error_size,
                        LabelPrinterProgressFn progress = nullptr);
