#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct FilaManLabelPreset {
  int id;
  char name[64];
  bool selected;
};

inline int filamanParseLabelPresets(const char* json, FilaManLabelPreset* out,
                                    size_t capacity, size_t* count,
                                    bool* selection_known) {
  if (count) *count = 0;
  if (selection_known) *selection_known = false;
  if (!json || !out || !count || !capacity) return -1;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return -2;
  JsonArrayConst presets = doc.as<JsonArrayConst>();
  if (presets.isNull() || presets.size() > capacity) return -3;
  bool any_selected_field = false;
  bool all_selected_fields = presets.size() != 0;
  size_t parsed = 0;
  for (JsonVariantConst value : presets) {
    JsonObjectConst preset = value.as<JsonObjectConst>();
    const int id = preset["id"] | -1;
    const char* name = preset["name"].as<const char*>();
    if (preset.isNull() || id <= 0 || !name || !name[0]) return -3;
    bool has_selected_field = false;
    for (JsonPairConst field : preset)
      if (strcmp(field.key().c_str(), "selected") == 0) has_selected_field = true;
    if (has_selected_field && !preset["selected"].is<bool>()) return -3;
    any_selected_field = any_selected_field || has_selected_field;
    all_selected_fields = all_selected_fields && has_selected_field;
    out[parsed].id = id;
    out[parsed].selected = has_selected_field && preset["selected"].as<bool>();
    size_t length = strlen(name);
    if (length >= sizeof(out[parsed].name)) {
      length = sizeof(out[parsed].name) - 1;
      while (length && ((unsigned char)name[length] & 0xC0) == 0x80) --length;
    }
    memcpy(out[parsed].name, name, length);
    out[parsed].name[length] = '\0';
    ++parsed;
  }
  if (any_selected_field != all_selected_fields) return -3;
  if (selection_known) *selection_known = parsed > 0 && all_selected_fields;
  *count = parsed;
  return 0;
}
