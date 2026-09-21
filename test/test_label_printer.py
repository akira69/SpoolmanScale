#!/usr/bin/env python3
"""Host checks for generic label-printer configuration and ordering."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / "Arduino.h").write_text("""
#pragma once
#include <string>
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string& value) : std::string(value) {}
  const char* c_str() const { return std::string::c_str(); }
};
""")
    prefs = tmp / "services" / "prefs_store.h"
    prefs.parent.mkdir()
    prefs.write_text("""
#pragma once
#include <Arduino.h>
int prefsGetInt(const char*, int);
String prefsGetString(const char*, const char* = "");
bool prefsGetBool(const char*, bool);
bool prefsPutInt(const char*, int);
bool prefsPutString(const char*, const char*);
bool prefsPutBool(const char*, bool);
""")
    source = r'''
#include <assert.h>
#include <cstring>
#include <map>
#include <string>
#include <Arduino.h>
#include "services/label_printer.h"

static std::map<std::string, int> ints;
static std::map<std::string, std::string> strings;
static std::map<std::string, bool> bools;
void clearPrefs() { ints.clear(); strings.clear(); bools.clear(); }
void putLegacy(const char* name, const char* address, int width, int height) {
  strings["m220_name"] = name;
  strings["m220_addr"] = address;
  ints["m220_media_w"] = width;
  ints["m220_media_h"] = height;
}
int prefsGetInt(const char* key, int fallback) {
  const auto it = ints.find(key); return it == ints.end() ? fallback : it->second;
}
String prefsGetString(const char* key, const char* fallback) {
  const auto it = strings.find(key); return it == strings.end() ? String(fallback) : String(it->second);
}
bool prefsGetBool(const char* key, bool fallback) {
  const auto it = bools.find(key); return it == bools.end() ? fallback : it->second;
}
bool prefsPutInt(const char* key, int value) { ints[key] = value; return true; }
bool prefsPutString(const char* key, const char* value) { strings[key] = value; return true; }
bool prefsPutBool(const char* key, bool value) { bools[key] = value; return true; }

static int print_calls = 0;
static LabelPrinterModel printed_model;
static LabelPrinterProgressFn printed_progress;
static const LabelRaster* printed_raster;
static std::string printed_address;
static bool print_result = true;
void progress() {}
size_t phomemoMSeriesScan(LabelPrinterDevice* out, size_t capacity,
                         const LabelPrinterConfig& selected, LabelPrinterProgressFn callback) {
  assert(capacity == 3 && selected.model == LabelPrinterModel::M110);
  assert(callback == progress);
  out[0] = {"speaker", "01:01:01:01:01:01"};
  out[1] = {"", "02:02:02:02:02:02"};
  out[2] = {"saved", "aa:aa:aa:aa:aa:aa"};
  return 3;
}
bool phomemoMSeriesPrint(LabelPrinterModel model, const char* address,
                        const LabelRaster& image, char*, size_t, LabelPrinterProgressFn callback) {
  ++print_calls;
  printed_model = model;
  printed_address = address;
  printed_raster = &image;
  printed_progress = callback;
  return print_result;
}

int main() {
  assert(!labelPrinterReachable());
  clearPrefs();
  LabelPrinterConfig fresh = labelPrinterLoadConfig();
  assert(fresh.model == LabelPrinterModel::M220);
  assert(!labelPrinterConfigured(fresh));
  assert(fresh.media_width_mm == 40 && fresh.media_length_mm == 30);

  putLegacy("Q123456789", "7e:11:22:33:44:55", 40, 30);
  LabelPrinterConfig legacy = labelPrinterLoadConfig();
  assert(legacy.model == LabelPrinterModel::M220);
  assert(strcmp(legacy.address, "7e:11:22:33:44:55") == 0);

  legacy.address[0] = '\0';
  legacy.name[0] = '\0';
  assert(labelPrinterSaveConfig(legacy));
  assert(!labelPrinterConfigured(labelPrinterLoadConfig()));

  LabelPrinterConfig m110 = legacy;
  m110.model = LabelPrinterModel::M110;
  m110.media_width_mm = 50;
  m110.media_length_mm = 30;
  assert(labelPrinterSaveConfig(m110));
  LabelPrinterConfig normalized = labelPrinterLoadConfig();
  assert(normalized.model == LabelPrinterModel::M110);
  assert(normalized.media_width_mm == 40);  // invalid 50 mm printable width resets

  assert(labelPrinterDotsForMm(40) == 320);
  assert(labelPrinterDotsForMm(30) == 240);
  assert(labelPrinterRasterWidth(LabelPrinterModel::M220, 40) == 576);
  assert(labelPrinterRasterWidth(LabelPrinterModel::M220, 75) == 600);
  LabelRaster media{576, 240, 72, nullptr, 0, 320, false};
  assert(labelPrinterRasterFits(LabelPrinterModel::M220, media, 40, 30));
  assert(!labelPrinterRasterFits(LabelPrinterModel::M220, media, 30, 40));
  LabelRaster rounded_50x80{576, 640, 72, nullptr, 0, 400, false};
  assert(labelPrinterRasterFits(LabelPrinterModel::M220, rounded_50x80, 50, 80));
  LabelRaster rounded_40x100{576, 800, 72, nullptr, 0, 320, false};
  assert(labelPrinterRasterFits(LabelPrinterModel::M220, rounded_40x100, 40, 100));
  LabelRaster rounded_down_64x33{576, 263, 72, nullptr, 0, 511, false};
  assert(labelPrinterRasterFits(LabelPrinterModel::M220, rounded_down_64x33, 64, 33));
  rounded_50x80.height = 641;
  assert(!labelPrinterRasterFits(LabelPrinterModel::M220, rounded_50x80, 50, 80));
  rounded_40x100.height = 797;
  assert(!labelPrinterRasterFits(LabelPrinterModel::M220, rounded_40x100, 40, 100));
  media.content_width = 480;
  assert(!labelPrinterRasterFits(LabelPrinterModel::M220, media, 40, 30));
  assert(labelPrinterRasterWidth(LabelPrinterModel::M110, 40) == 384);
  assert(labelPrinterStartupCrash("label printer BLE start", true));
  assert(!labelPrinterStartupCrash("label printer BLE start", false));
  assert(!labelPrinterStartupCrash("label printer connect", true));
  uint8_t pixels[48 * 240] = {};
  LabelRaster raster{384, 240, 48, pixels, sizeof(pixels), 320, false};
  assert(labelPrinterRasterFits(LabelPrinterModel::M110, raster, 40, 30));
  assert(!labelPrinterRasterFits(LabelPrinterModel::M110, raster, 48, 30));

  LabelPrinterConfig selected = normalized;
  strcpy(selected.address, "aa:aa:aa:aa:aa:aa");
  char error[128] = "stale";
  assert(labelPrinterPrint(selected, raster, error, sizeof(error), progress));
  assert(labelPrinterReachable());
  assert(print_calls == 1 && printed_model == LabelPrinterModel::M110);
  assert(printed_address == selected.address && printed_raster == &raster);
  assert(printed_progress == progress && error[0] == '\0');
  print_result = false;
  assert(!labelPrinterPrint(selected, raster, error, sizeof(error)));
  assert(!labelPrinterReachable());
  assert(print_calls == 2);
  auto rejected = [&](const LabelPrinterConfig& config, const LabelRaster& image) {
    error[0] = '\0';
    assert(!labelPrinterPrint(config, image, error, sizeof(error)));
    assert(print_calls == 2 && error[0]);
  };
  LabelPrinterConfig invalid = selected;
  invalid.model = LabelPrinterModel::NONE;
  rejected(invalid, raster);
  invalid.model = static_cast<LabelPrinterModel>(99);
  rejected(invalid, raster);
  invalid = selected;
  invalid.address[0] = '\0';
  rejected(invalid, raster);
  invalid = selected;
  invalid.media_length_mm = 40;
  rejected(invalid, raster);
  assert(strstr(error, "loaded media"));
  LabelRaster broken = raster;
  broken.pixels = nullptr;
  rejected(selected, broken);
  uint8_t padded[49] = {};
  padded[48] = 1;
  broken = {385, 1, 49, padded, sizeof(padded), 320, false};
  rejected(selected, broken);
  assert(strstr(error, "Invalid label image"));
  padded[48] = 0;
  rejected(selected, broken);  // valid raster exceeds the M110 print head
  assert(strstr(error, "print head"));
  uint8_t m220_pixels[72 * 240] = {};
  LabelRaster m220_raster{576, 240, 72, m220_pixels, sizeof(m220_pixels), 320, false};
  LabelPrinterConfig m220 = selected;
  m220.model = LabelPrinterModel::M220;
  print_result = true;
  assert(labelPrinterPrint(m220, m220_raster, error, sizeof(error)));
  assert(print_calls == 3 && printed_model == LabelPrinterModel::M220);

  LabelPrinterDevice scanned[3] = {};
  assert(labelPrinterScan(selected, scanned, 3, progress) == 3);
  assert(labelPrinterReachable());
  assert(strcmp(scanned[0].address, selected.address) == 0);
  assert(strcmp(scanned[1].name, "speaker") == 0 && !scanned[2].name[0]);

  LabelPrinterDevice devices[] = {
    {{0}, "dd:dd:dd:dd:dd:dd"},
    {"speaker", "cc:cc:cc:cc:cc:cc"},
    {"Q123456789", "bb:bb:bb:bb:bb:bb"},
    {"m110-label", "ee:ee:ee:ee:ee:ee"},
    {"saved", "aa:aa:aa:aa:aa:aa"},
  };
  labelPrinterSortDevices(devices, 5, selected);
  assert(strcmp(devices[0].address, selected.address) == 0);
  assert(strcmp(devices[1].name, "m110-label") == 0);
  assert(strcmp(devices[2].name, "Q123456789") == 0);
  assert(devices[4].name[0] == '\0');

  LabelPrinterDevice kept[3] = {};
  size_t kept_count = 0;
  const LabelPrinterDevice candidates[] = {
    {"speaker", "01:01:01:01:01:01"},
    {"watch", "02:02:02:02:02:02"},
    {"keyboard", "03:03:03:03:03:03"},
    {"saved", "aa:aa:aa:aa:aa:aa"},
    {"Q123456789", "04:04:04:04:04:04"},
    {"Q123456789", "04:04:04:04:04:04"},
  };
  for (const auto& candidate : candidates)
    labelPrinterConsiderDevice(kept, &kept_count, 3, candidate, selected);
  assert(strcmp(kept[0].name, "speaker") == 0); // collection preserves arrival order
  labelPrinterSortDevices(kept, kept_count, selected);
  assert(kept_count == 3);
  assert(strcmp(kept[0].address, selected.address) == 0);
  assert(strcmp(kept[1].name, "Q123456789") == 0);
  // Equal-priority devices retain their first-arrival order after eviction/sort.
  kept_count = 0;
  for (size_t i = 0; i < 4; ++i)
    labelPrinterConsiderDevice(kept, &kept_count, 3, candidates[i], selected);
  labelPrinterSortDevices(kept, kept_count, selected);
  assert(strcmp(kept[1].name, "speaker") == 0 && strcmp(kept[2].name, "watch") == 0);
  LabelPrinterDevice unnamed = {"", "05:05:05:05:05:05"};
  LabelPrinterDevice named = {"m110-label", "05:05:05:05:05:05"};
  kept_count = 0;
  labelPrinterConsiderDevice(kept, &kept_count, 3, unnamed, selected);
  labelPrinterConsiderDevice(kept, &kept_count, 3, named, selected);
  labelPrinterConsiderDevice(kept, &kept_count, 3, unnamed, selected);
  assert(kept_count == 1 && strcmp(kept[0].name, "m110-label") == 0);
}
'''
    result = subprocess.run(
        ["g++", "-std=c++11", f"-I{tmp}", f"-I{root / 'src'}", "-x", "c++", "-",
         str(root / "src/services/label_printer.cpp"), "-o", str(tmp / "check")],
        input=source, text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(tmp / "check")], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
