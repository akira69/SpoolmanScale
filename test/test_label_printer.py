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

int main() {
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

  assert(labelPrinterRasterWidth(LabelPrinterModel::M220, 40) == 576);
  assert(labelPrinterRasterWidth(LabelPrinterModel::M110, 40) == 384);
  uint8_t pixels[48 * 240] = {};
  LabelRaster raster{384, 240, 48, pixels, sizeof(pixels), 320, false};
  assert(labelPrinterRasterFits(LabelPrinterModel::M110, raster, 40, 30));
  assert(!labelPrinterRasterFits(LabelPrinterModel::M110, raster, 48, 30));

  LabelPrinterConfig selected = normalized;
  strcpy(selected.address, "aa:aa:aa:aa:aa:aa");
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
  labelPrinterSortDevices(kept, kept_count, selected);
  assert(kept_count == 3);
  assert(strcmp(kept[0].address, selected.address) == 0);
  assert(strcmp(kept[1].name, "Q123456789") == 0);
}
'''
    result = subprocess.run(
        ["g++", "-std=c++11", f"-I{tmp}", f"-I{root / 'src'}", "-x", "c++", "-",
         str(root / "src/services/label_printer.cpp"), "-o", str(tmp / "check")],
        input=source, text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(tmp / "check")], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
