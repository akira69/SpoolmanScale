#include "services/filaman_labels.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "services/filaman_api_key.h"
#include "services/http_progress.h"

namespace {
bool parseHeaderNumber(const String& value, uint16_t* out) {
  if (!value.length()) return false;
  unsigned n = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c < '0' || c > '9') return false;
    n = n * 10 + c - '0';
    if (n > UINT16_MAX) return false;
  }
  *out = n;
  return true;
}

bool parseHeaderId(const String& value, int* out) {
  if (!value.length()) return false;
  unsigned long n = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c < '0' || c > '9') return false;
    n = n * 10 + c - '0';
    if (n > INT_MAX) return false;
  }
  *out = int(n);
  return true;
}
}

void filamanFreeLabel(LabelRaster* image) {
  if (!image) return;
  free(image->pixels);
  *image = {};
}

int filamanFetchMonoLabel(const char* base_url, const char* api_key, int spool_id,
                          int preset_id, uint16_t requested_width, const char* orientation,
                          LabelRaster* out, int* resolved_preset_id,
                          uint32_t timeout_ms) {
  if (!out) return -1;
  *out = {};
  if (resolved_preset_id) *resolved_preset_id = -1;
  if (!base_url || strlen(base_url) <= 7 || !api_key || !api_key[0] ||
      spool_id <= 0 || preset_id < 0 || requested_width < 384 ||
      (!orientation || (strcmp(orientation, "landscape") && strcmp(orientation, "portrait"))) ||
      requested_width > 1024) return -1;

  HttpStallTime stall;
  String url = String(base_url) + "/api/v1/labels/spool/" + spool_id +
               "/render?format=mono1&dpi=203&align=right&orientation=" + orientation +
               "&width=" + requested_width;
  if (preset_id) url += String("&preset_id=") + preset_id;
  HTTPClient http;
  if (!http.begin(url)) return -1;
  http.setTimeout(timeout_ms);
  http.setReuse(false);
  const char* headers[] = {"X-Preset-Id", "X-Image-Width", "X-Image-Height", "X-Row-Bytes", "X-Bit-Order", "X-Content-Width", "X-Rotated"};
  http.collectHeaders(headers, 7);
  addApiKey(http, api_key);
  const int code = http.GET();
  if (code != 200) { http.end(); return code; }

  LabelRaster image{};
  int header_preset_id = 0;
  const bool headers_ok = parseHeaderId(http.header("X-Preset-Id"), &header_preset_id) &&
                          parseHeaderNumber(http.header("X-Image-Width"), &image.width) &&
                          parseHeaderNumber(http.header("X-Image-Height"), &image.height) &&
                          parseHeaderNumber(http.header("X-Row-Bytes"), &image.row_bytes) &&
                          parseHeaderNumber(http.header("X-Content-Width"), &image.content_width) &&
                          (http.header("X-Rotated") == "0" || http.header("X-Rotated") == "1") &&
                          http.header("X-Bit-Order") == "msb-black-1";
  image.rotated = http.header("X-Rotated") == "1";
  const int declared = http.getSize();
  image.length = declared > 0 ? size_t(declared) : 0;
  if (!headers_ok || image.width != requested_width || !image.content_width ||
      image.content_width > image.width ||
      !labelRasterShapeValid(image.width, image.height, image.row_bytes, image.length)) {
    http.end();
    return -2;
  }
  image.pixels = static_cast<uint8_t*>(heap_caps_malloc(image.length, MALLOC_CAP_SPIRAM));
  if (!image.pixels) { http.end(); return FILAMAN_LABEL_NO_PSRAM; }
  WiFiClient* raw = http.getStreamPtr();
  HttpProgressStream progress(*raw);
  Stream* input = httpProgressActive()
      ? static_cast<Stream*>(&progress) : static_cast<Stream*>(raw);
  size_t used = 0;
  while (used < image.length) {
    const size_t got = input->readBytes(image.pixels + used, image.length - used);
    if (!got) break;
    used += got;
  }
  // A Content-Length mismatch is rejected above; also catch surplus bytes
  // already present on the socket before handing the raster to a printer.
  const bool valid = used == image.length && !raw->available() &&
                     labelRasterPaddingValid(image);
  http.end();
  if (!valid) { filamanFreeLabel(&image); return -2; }
  *out = image;
  if (resolved_preset_id) *resolved_preset_id = header_preset_id;
  return code;
}
