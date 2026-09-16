#include <Arduino.h>
#include <HTTPClient.h>
#include <stdint.h>
#include <string.h>

#include "services/filaman_print_request_parse.h"
#include "services/filaman_api_key.h"

int filamanRequestLabelPrint(const char* base_url, const char* api_key, int spool_id,
                             int preset_id, int* request_id, uint32_t timeout_ms) {
  if (request_id) *request_id = 0;
  if (!request_id || !base_url || strlen(base_url) <= 7 || !api_key || !api_key[0] ||
      spool_id <= 0 || preset_id < 0) return -1;
  String url = String(base_url) + "/api/v1/labels/spool/" + spool_id + "/print-request";
  if (preset_id) url += String("?preset_id=") + preset_id;
  HTTPClient http;
  http.begin(url);
  http.setTimeout(timeout_ms);
  addApiKey(http, api_key);
  const int code = http.POST("");
  if (code != 201) { http.end(); return code; }
  const String body = http.getString();
  http.end();
  return filamanParsePrintRequest(body.c_str(), spool_id, preset_id, request_id);
}
