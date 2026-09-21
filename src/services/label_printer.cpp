#include "services/label_printer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "services/prefs_store.h"
#include "services/phomemo_m_series.h"

namespace {
static const LabelPrinterProfile kNone = {};
static const LabelPrinterProfile kM220 = {
  LabelPrinterModel::M220, "M220", 40, 30, 20, 75, 10, 150, 576, 648, false
};
static const LabelPrinterProfile kM110 = {
  LabelPrinterModel::M110, "M110", 40, 30, 20, 48, 10, 150, 384, 384, true
};
bool s_reachable = false;
LabelPrinterConfig s_config{};
bool s_config_loaded = false;

bool dimensionsValid(const LabelPrinterProfile& profile, uint16_t width, uint16_t length) {
  return profile.model != LabelPrinterModel::NONE &&
         width >= profile.min_width_mm && width <= profile.max_width_mm &&
         length >= profile.min_length_mm && length <= profile.max_length_mm;
}

void copyString(char* out, size_t size, const char* value) {
  strncpy(out, value ? value : "", size - 1);
  out[size - 1] = '\0';
}

LabelPrinterConfig normalizedConfig(const LabelPrinterConfig& input) {
  const LabelPrinterProfile& profile = labelPrinterProfile(input.model);
  const LabelPrinterProfile& selected = profile.model == LabelPrinterModel::NONE ? kM220 : profile;
  LabelPrinterConfig config{};
  config.model = selected.model;
  copyString(config.name, sizeof(config.name), input.name);
  copyString(config.address, sizeof(config.address), input.address);
  config.media_width_mm = input.media_width_mm;
  config.media_length_mm = input.media_length_mm;
  if (!dimensionsValid(selected, config.media_width_mm, config.media_length_mm)) {
    config.media_width_mm = selected.default_width_mm;
    config.media_length_mm = selected.default_length_mm;
  }
  return config;
}

bool containsCaseInsensitive(const char* text, const char* needle) {
  if (!text || !needle || !*needle) return false;
  for (; *text; ++text) {
    const char* a = text;
    const char* b = needle;
    while (*a && *b && tolower(static_cast<unsigned char>(*a)) ==
                      tolower(static_cast<unsigned char>(*b))) {
      ++a;
      ++b;
    }
    if (!*b) return true;
  }
  return false;
}

bool matchesRenderedDimension(uint16_t pixels, uint16_t mm) {
  const uint16_t dots = labelPrinterDotsForMm(mm);
  // FilaMan derives the second edge from the already-rounded first edge, so
  // it can differ by one pixel from a direct mm-to-dot result.
  return pixels >= dots - 1 && pixels <= dots + 1;
}
}

const LabelPrinterProfile& labelPrinterProfile(LabelPrinterModel model) {
  switch (model) {
    case LabelPrinterModel::M220: return kM220;
    case LabelPrinterModel::M110: return kM110;
    default: return kNone;
  }
}

LabelPrinterConfig labelPrinterLoadConfig() {
  if (s_config_loaded) return s_config;
  LabelPrinterConfig config{};
  if (!prefsGetBool("printer_mig", false)) {
    config.model = LabelPrinterModel::M220;
    copyString(config.name, sizeof(config.name), prefsGetString("m220_name", "").c_str());
    copyString(config.address, sizeof(config.address), prefsGetString("m220_addr", "").c_str());
    config.media_width_mm = prefsGetInt("m220_media_w", kM220.default_width_mm);
    config.media_length_mm = prefsGetInt("m220_media_h", kM220.default_length_mm);
  } else {
    config.model = static_cast<LabelPrinterModel>(prefsGetInt("printer_model", int(LabelPrinterModel::M220)));
    copyString(config.name, sizeof(config.name), prefsGetString("printer_name", "").c_str());
    copyString(config.address, sizeof(config.address), prefsGetString("printer_addr", "").c_str());
    config.media_width_mm = prefsGetInt("printer_w", kM220.default_width_mm);
    config.media_length_mm = prefsGetInt("printer_h", kM220.default_length_mm);
  }
  s_config = normalizedConfig(config);
  s_config_loaded = true;
  return s_config;
}

bool labelPrinterSaveConfig(const LabelPrinterConfig& input) {
  const LabelPrinterConfig config = normalizedConfig(input);
  bool saved = true;
  saved = prefsPutInt("printer_model", int(config.model)) && saved;
  saved = prefsPutString("printer_addr", config.address) && saved;
  saved = prefsPutString("printer_name", config.name) && saved;
  saved = prefsPutInt("printer_w", config.media_width_mm) && saved;
  saved = prefsPutInt("printer_h", config.media_length_mm) && saved;
  saved = saved && prefsPutBool("printer_mig", true);
  if (saved) {
    s_config = config;
    s_config_loaded = true;
  }
  return saved;
}

bool labelPrinterConfigured(const LabelPrinterConfig& config) {
  return config.address[0] != '\0';
}

bool labelPrinterReachable() { return s_reachable; }

void labelPrinterSetReachable(bool reachable) { s_reachable = reachable; }

#ifdef UNIT_TEST
void labelPrinterResetConfigCache() { s_config_loaded = false; }
#endif

bool labelPrinterStartupCrash(const char* previous_crumb, bool panic_reset) {
  return panic_reset && previous_crumb &&
         !strcmp(previous_crumb, LABEL_PRINTER_BLE_START_CRUMB);
}

size_t labelPrinterScan(const LabelPrinterConfig& selected,
                       LabelPrinterDevice* out, size_t capacity,
                       LabelPrinterProgressFn progress) {
  const size_t count = phomemoMSeriesScan(out, capacity, selected, progress);
  if (labelPrinterConfigured(selected)) {
    labelPrinterSetReachable(false);
    for (size_t i = 0; i < count; ++i)
      if (!strcmp(out[i].address, selected.address)) {
        labelPrinterSetReachable(true);
        break;
      }
  }
  labelPrinterSortDevices(out, count, selected);
  return count;
}

bool labelPrinterPrint(const LabelPrinterConfig& config, const LabelRaster& image,
                       char* error, size_t error_size, LabelPrinterProgressFn progress) {
  if (error_size) error[0] = 0;
  const LabelPrinterProfile& profile = labelPrinterProfile(config.model);
  const char* message = nullptr;
  if (profile.model == LabelPrinterModel::NONE || !labelPrinterConfigured(config))
    message = "Select a label printer first.";
  else if (!labelRasterPaddingValid(image))
    message = "Invalid label image.";
  else if (image.width > profile.max_raster_width)
    message = "Label width exceeds print head.";
  else if (!labelPrinterRasterFits(config.model, image, config.media_width_mm, config.media_length_mm))
    message = "Label does not fit loaded media.";
  if (message) {
    if (error_size) snprintf(error, error_size, "%s", message);
    return false;
  }
  const bool printed = phomemoMSeriesPrint(config.model, config.address, image,
                                           error, error_size, progress);
  labelPrinterSetReachable(printed);
  return printed;
}

uint16_t labelPrinterDotsForMm(uint16_t mm) {
  return (uint32_t(mm) * 2030 + 127) / 254;
}

uint16_t labelPrinterRasterWidth(LabelPrinterModel model, uint16_t media_width_mm) {
  const LabelPrinterProfile& profile = labelPrinterProfile(model);
  if (profile.model == LabelPrinterModel::NONE) return 0;
  const uint16_t content = labelPrinterDotsForMm(media_width_mm);
  const uint16_t canvas = content > profile.base_raster_width ? content : profile.base_raster_width;
  const uint16_t width = (canvas + 7) & ~uint16_t(7);
  return width > profile.max_raster_width ? profile.max_raster_width : width;
}

bool labelPrinterRasterFits(LabelPrinterModel model, const LabelRaster& image,
                            uint16_t media_width_mm, uint16_t media_length_mm) {
  const LabelPrinterProfile& profile = labelPrinterProfile(model);
  return dimensionsValid(profile, media_width_mm, media_length_mm) &&
         image.width == labelPrinterRasterWidth(model, media_width_mm) &&
         matchesRenderedDimension(image.content_width, media_width_mm) &&
         matchesRenderedDimension(image.height, media_length_mm);
}

uint8_t labelPrinterDevicePriority(const LabelPrinterDevice& device,
                                   const LabelPrinterConfig& selected) {
  if (selected.address[0] && !strcmp(device.address, selected.address)) return 0;
  const LabelPrinterProfile& profile = labelPrinterProfile(selected.model);
  if (profile.model != LabelPrinterModel::NONE && containsCaseInsensitive(device.name, profile.name)) return 1;
  if (strlen(device.name) >= 10 && (device.name[0] == 'Q' || device.name[0] == 'q')) return 2;
  if (containsCaseInsensitive(device.name, "PHOMEMO")) return 3;
  if (!device.name[0]) return 5;
  return 4;
}

void labelPrinterSortDevices(LabelPrinterDevice* devices, size_t count,
                             const LabelPrinterConfig& selected) {
  for (size_t i = 1; i < count; ++i) {
    const LabelPrinterDevice device = devices[i];
    const uint8_t priority = labelPrinterDevicePriority(device, selected);
    size_t j = i;
    while (j && priority < labelPrinterDevicePriority(devices[j - 1], selected)) {
      devices[j] = devices[j - 1];
      --j;
    }
    devices[j] = device;
  }
}

void labelPrinterConsiderDevice(LabelPrinterDevice* devices, size_t* count,
                                size_t capacity,
                                const LabelPrinterDevice& candidate,
                                const LabelPrinterConfig& selected) {
  if (!devices || !count || !capacity) return;
  for (size_t i = 0; i < *count; ++i)
    if (!strcmp(devices[i].address, candidate.address)) {
      if (!devices[i].name[0] && candidate.name[0]) devices[i] = candidate;
      return;
    }
  if (*count < capacity) {
    devices[(*count)++] = candidate;
    return;
  }
  // Evict the latest worst-priority entry so equal-priority arrivals stay stable.
  size_t worst = 0;
  for (size_t i = 1; i < *count; ++i)
    if (labelPrinterDevicePriority(devices[i], selected) >=
        labelPrinterDevicePriority(devices[worst], selected)) worst = i;
  if (labelPrinterDevicePriority(candidate, selected) <
      labelPrinterDevicePriority(devices[worst], selected)) {
    for (size_t i = worst + 1; i < *count; ++i) devices[i - 1] = devices[i];
    devices[*count - 1] = candidate;
  }
}
