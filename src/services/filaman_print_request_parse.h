#pragma once

#include <ArduinoJson.h>
#include <string.h>

// A 201 is usable only when it identifies the request and the requested spool/preset.
inline int filamanParsePrintRequest(const char* body, int spool_id, int preset_id, int* request_id) {
  if (request_id) *request_id = 0;
  if (!body || !request_id) return -2;
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) return -2;
  const JsonVariantConst id = doc["id"];
  const JsonVariantConst spool = doc["spool_id"];
  const JsonVariantConst preset = doc["preset_id"];
  bool has_preset = false;
  for (JsonPairConst field : doc.as<JsonObjectConst>())
    if (strcmp(field.key().c_str(), "preset_id") == 0) has_preset = true;
  if (!has_preset) return -2;
  if (!id.is<int>() || id.as<int>() <= 0 || !spool.is<int>() || spool.as<int>() != spool_id ||
      (preset_id == 0 ? !preset.isNull() : (!preset.is<int>() || preset.as<int>() != preset_id))) return -2;
  *request_id = id.as<int>();
  return 201;
}
