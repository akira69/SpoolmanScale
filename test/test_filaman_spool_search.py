#!/usr/bin/env python3
"""Host checks for FilaMan spool search and preset selection requests."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
json_include = root / ".pio/libdeps/wt32-sc01-plus/ArduinoJson/src"

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)

    def header(name, body):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#pragma once\n" + body)

    header("Arduino.h", r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  String(int n) : std::string(std::to_string(n)) {}
  String operator+(const char* s) const { return String(std::string(*this) + s); }
  String operator+(int n) const { return *this + std::to_string(n).c_str(); }
  String& operator+=(char c) { std::string::operator+=(c); return *this; }
  String& operator+=(const char* s) { std::string::operator+=(s); return *this; }
  String& operator+=(const String& s) { std::string::operator+=(s); return *this; }
  size_t write(uint8_t c) { push_back((char)c); return 1; }
  size_t write(const uint8_t* s, size_t n) { append((const char*)s, n); return n; }
  String substring(size_t start, size_t count = std::string::npos) const {
    return String(substr(start, count));
  }
};
inline String operator+(const char* a, const String& b) { return String(std::string(a) + b); }
inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}
''')
    header("Stream.h", r'''
#include <stddef.h>
#include <stdint.h>
class Stream {
 public:
  virtual ~Stream() {}
  virtual int read() { return -1; }
  virtual int read(uint8_t*, size_t) { return 0; }
  virtual int peek() { return -1; }
  virtual void flush() {}
  virtual size_t write(uint8_t) { return 0; }
  virtual size_t readBytes(char*, size_t) { return 0; }
  virtual int available() { return 0; }
};
''')
    header("HTTPClient.h", r'''
#include <Arduino.h>
#include <Stream.h>
#include <algorithm>
extern std::string last_url, last_body, last_content_type, response_body;
extern int response_code;
class WiFiClient : public Stream {
 public:
  using Stream::read;
  size_t offset = 0;
  int read() override { return offset < response_body.size() ? (unsigned char)response_body[offset++] : -1; }
  int peek() override { return offset < response_body.size() ? (unsigned char)response_body[offset] : -1; }
  int available() override { return (int)(response_body.size() - offset); }
  size_t readBytes(char* out, size_t n) override {
    n = std::min(n, response_body.size() - offset);
    memcpy(out, response_body.data() + offset, n); offset += n; return n;
  }
};
class HTTPClient {
 public:
  bool begin(const String& url) { last_url = url; return true; }
  void setConnectTimeout(uint32_t) {}
  void setTimeout(uint32_t) {}
  void setReuse(bool) {}
  void addHeader(const char* key, const char* value) {
    if (std::string(key) == "Content-Type") last_content_type = value;
  }
  void addHeader(const char* key, const String& value) { addHeader(key, value.c_str()); }
  void collectHeaders(const char**, size_t) {}
  int GET() { getStreamPtr()->offset = 0; return response_code; }
  int POST(const char* body) { last_body = body; return response_code; }
  int POST(const String& body) { return POST(body.c_str()); }
  int PATCH(const char* body) { last_body = body; return response_code; }
  int PATCH(const String& body) { return PATCH(body.c_str()); }
  int PUT(const char* body) { last_body = body; return response_code; }
  int PUT(const String& body) { return PUT(body.c_str()); }
  int getSize() { return (int)response_body.size(); }
  bool connected() { return false; }
  WiFiClient* getStreamPtr() { static WiFiClient stream; return &stream; }
  WiFiClient& getStream() { return *getStreamPtr(); }
  String getString() { return String(response_body); }
  String header(const char*) { return String(""); }
  void end() {}
};
''')
    header("esp_heap_caps.h", r'''
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
extern size_t last_allocation;
inline void* heap_caps_malloc(size_t n, int) { last_allocation = n; return malloc(n); }
inline void* heap_caps_realloc(void* p, size_t n, int) { return realloc(p, n); }
inline void heap_caps_free(void* p) { free(p); }
''')
    header("hardware/sd_logger.h", "extern bool sd_verbose;\ninline void logSD(const char*) {}\ninline void logSDf(const char*, ...) {}\n")
    header("services/filaman_api_key.h", r'''
#include <HTTPClient.h>
inline void addApiKey(HTTPClient&, const char*) {}
''')

    source = r'''
#include <assert.h>
#include <string>
#include <ArduinoJson.h>
#include "services/filaman_api.h"
std::string last_url, last_body, last_content_type, response_body;
int response_code = 200;
size_t last_allocation = 0;
bool sd_verbose = false;
int main() {
  JsonDocument doc;
  int total = 0;
  response_body = "{\"items\":[],\"total\":0}";
  assert(filamanGetSpoolPageJson("http://fila", "key", 2, 10, doc, &total,
                                "#PLA & blue", 8000) == 200);
  assert(last_url == "http://fila/api/v1/spools?page=2&page_size=10&sort_by=id&sort_order=desc&search=%23PLA%20%26%20blue");

  response_code = 204;
  assert(filamanSelectLabelPreset("http://fila", "key", 42, 8000) == 204);
  assert(last_url == "http://fila/api/v1/me/label-presets/selection");
  assert(last_content_type == "application/json");
  assert(last_body == "{\"preset_id\":42}");
  assert(filamanSelectLabelPreset("http://fila", "key", 0, 8000) == 204);
  assert(last_body == "{\"preset_id\":null}");
  assert(filamanSelectLabelPreset("http://fila", "key", -1, 8000) == -1);

  response_code = 200;
  response_body = "[{\"id\":7,\"name\":\"Plain\",\"selected\":true}]";
  FilaManLabelPreset presets[1];
  size_t count = 0;
  bool selection_known = false;
  assert(filamanListLabelPresets("http://fila", "key", presets, 1, &count,
                                 &selection_known, 8000) == 200);
  assert(last_allocation == response_body.size() + 1);
  assert(count == 1 && presets[0].id == 7 && selection_known);
}
'''
    result = subprocess.run(
        ["g++", "-std=c++11", "-ffunction-sections", "-fdata-sections",
         f"-I{tmp}", f"-I{root / 'src'}", f"-I{json_include}", "-x", "c++", "-",
         str(root / "src/services/filaman_api.cpp"), "-Wl,-dead_strip", "-o", str(tmp / "check")],
        input=source, text=True, capture_output=True,
    )
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(tmp / "check")], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
