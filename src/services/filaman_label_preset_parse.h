#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct FilaManLabelPreset {
  int id;
  char name[64];
};

inline int filamanParseLabelPresets(const char* json, FilaManLabelPreset* out,
                                    size_t capacity, size_t* count) {
  if (count) *count = 0;
  if (!json || !out || !count || !capacity) return -1;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return -2;
  JsonArrayConst presets = doc.as<JsonArrayConst>();
  if (presets.isNull() || presets.size() > capacity) return -3;
  size_t parsed = 0;
  for (JsonVariantConst value : presets) {
    JsonObjectConst preset = value.as<JsonObjectConst>();
    const int id = preset["id"] | -1;
    const char* name = preset["name"].as<const char*>();
    if (preset.isNull() || id <= 0 || !name || !name[0] ||
        strlen(name) >= sizeof(out[parsed].name)) return -3;
    out[parsed].id = id;
    strncpy(out[parsed].name, name, sizeof(out[parsed].name) - 1);
    out[parsed].name[sizeof(out[parsed].name) - 1] = '\0';
    ++parsed;
  }
  *count = parsed;
  return 0;
}
