#include "services/label_printer.h"

#include <ctype.h>
#include <string.h>

#include "services/prefs_store.h"

namespace {
static const LabelPrinterProfile kNone = {};
static const LabelPrinterProfile kM220 = {
  LabelPrinterModel::M220, "M220", 40, 30, 20, 75, 10, 150, 576, 648, false
};
static const LabelPrinterProfile kM110 = {
  LabelPrinterModel::M110, "M110", 40, 30, 20, 48, 10, 150, 384, 384, true
};

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
}

const LabelPrinterProfile& labelPrinterProfile(LabelPrinterModel model) {
  switch (model) {
    case LabelPrinterModel::M220: return kM220;
    case LabelPrinterModel::M110: return kM110;
    default: return kNone;
  }
}

LabelPrinterConfig labelPrinterLoadConfig() {
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
  return normalizedConfig(config);
}

bool labelPrinterSaveConfig(const LabelPrinterConfig& input) {
  const LabelPrinterConfig config = normalizedConfig(input);
  bool saved = true;
  saved = prefsPutInt("printer_model", int(config.model)) && saved;
  saved = prefsPutString("printer_addr", config.address) && saved;
  saved = prefsPutString("printer_name", config.name) && saved;
  saved = prefsPutInt("printer_w", config.media_width_mm) && saved;
  saved = prefsPutInt("printer_h", config.media_length_mm) && saved;
  return saved && prefsPutBool("printer_mig", true);
}

bool labelPrinterConfigured(const LabelPrinterConfig& config) {
  return config.address[0] != '\0';
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
         image.content_width == labelPrinterDotsForMm(media_width_mm) &&
         image.height == labelPrinterDotsForMm(media_length_mm);
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
    if (!strcmp(devices[i].address, candidate.address)) return;
  if (*count < capacity) {
    devices[(*count)++] = candidate;
    labelPrinterSortDevices(devices, *count, selected);
    return;
  }
  if (labelPrinterDevicePriority(candidate, selected) <
      labelPrinterDevicePriority(devices[*count - 1], selected)) {
    devices[*count - 1] = candidate;
    labelPrinterSortDevices(devices, *count, selected);
  }
}
