#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

inline void addApiKey(HTTPClient& http, const char* api_key) {
  if (api_key && api_key[0])
    http.addHeader("Authorization", String("ApiKey ") + api_key);
}
