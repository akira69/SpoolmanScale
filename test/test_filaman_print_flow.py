#!/usr/bin/env python3
"""Host checks for print request wire behavior and deferred state."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    (d / 'Arduino.h').write_text('''
#pragma once
#include <string>
class String {
 public:
  std::string value;
  String(const char* s) : value(s) {}
  String(int n) : value(std::to_string(n)) {}
  String(std::string s) : value(s) {}
  String operator+(const char* s) const { return String(value + s); }
  String operator+(int n) const { return String(value + std::to_string(n)); }
  String& operator+=(const String& s) { value += s.value; return *this; }
  const char* c_str() const { return value.c_str(); }
};
''')
    (d / 'HTTPClient.h').write_text('''
#pragma once
#include "Arduino.h"
extern std::string seen_url, seen_auth, seen_body;
extern int posts, response_code;
extern std::string response_body;
class HTTPClient {
 public:
  void begin(const String& s) { seen_url = s.value; }
  void setTimeout(unsigned long) {}
  void addHeader(const char* name, const String& s) { if (std::string(name) == "Authorization") seen_auth = s.value; }
  int POST(const char* body) { ++posts; seen_body = body; return response_code; }
  String getString() { return String(response_body); }
  void end() {}
};
''')
    source = r'''
#include <assert.h>
#include <string>
#include "services/filaman_print_pending.h"
std::string seen_url, seen_auth, seen_body, response_body;
int posts = 0, response_code = 201;
int filamanRequestLabelPrint(const char*, const char*, int, int, int*, uint32_t);
int main() {
  int id = 0;
  response_body = "{\"id\":42,\"spool_id\":123,\"preset_id\":null}";
  assert(filamanRequestLabelPrint("http://fila", "uak.secret", 123, 0, &id, 8000) == 201 && id == 42);
  assert(seen_url == "http://fila/api/v1/labels/spool/123/print-request");
  assert(seen_auth == "ApiKey uak.secret" && posts == 1);
  response_body = "{\"id\":43,\"spool_id\":123,\"preset_id\":7}";
  assert(filamanRequestLabelPrint("http://fila", "uak.secret", 123, 7, &id, 8000) == 201 && id == 43);
  assert(seen_url == "http://fila/api/v1/labels/spool/123/print-request?preset_id=7");
  response_code = -11;
  assert(filamanRequestLabelPrint("http://fila", "uak.secret", 123, 7, &id, 8000) == -11 && id == 0 && posts == 3);
  FilaManPrintPending pending;
  assert(pending.request(123, 7));
  assert(!pending.request(124, 8));
  int spool = 0, preset = 0;
  assert(pending.take(&spool, &preset) && spool == 123 && preset == 7);
  assert(!pending.take(&spool, &preset));
  assert(pending.request(123, 7));
  pending.cancel();
  assert(!pending.take(&spool, &preset));
}
'''
    result = subprocess.run(['g++', '-std=c++11', f'-I{d}', f'-I{root / "src"}', f'-I{root / ".pio/libdeps/wt32-sc01-plus/ArduinoJson/src"}', '-x', 'c++', '-', str(root / 'src/services/filaman_print_request.cpp'), '-o', str(d / 'check')], input=source, text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(d / 'check')], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr

# Raster reads must advance the active overlay and still reject surplus data.
with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    def header(name, text):
        path = d / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#pragma once\n' + text)
    header('Arduino.h', r'''
#include <string>
#include <cstring>
#include <stdint.h>
class String : public std::string {
public:
  using std::string::string;
  String(std::string s) : std::string(s) {}
  String(int n) : std::string(std::to_string(n)) {}
  String operator+(const char* s) const { return String(std::string(*this)+s); }
  String operator+(int n) const { return *this + std::to_string(n).c_str(); }
};
''')
    header('Stream.h', r'''
#include <stddef.h>
#include <stdint.h>
class Stream { public: virtual ~Stream() {} virtual size_t readBytes(char*,size_t)=0;
size_t readBytes(uint8_t* p,size_t n) { return readBytes(reinterpret_cast<char*>(p),n); }
virtual int available()=0; };
''')
    header('HTTPClient.h', r'''
#include <Arduino.h>
#include <Stream.h>
#include <algorithm>
#include <map>
extern std::map<std::string,std::string> headers;
extern std::string seen_url;
extern size_t bytes_left;
extern int declared;
class WiFiClient : public Stream {
public: using Stream::readBytes;
size_t readBytes(char* p,size_t n) override { n=std::min(n,bytes_left); memset(p,0,n); bytes_left-=n; return n; }
int available() override { return bytes_left; }
};
class HTTPClient {
public:
 bool begin(const String& url) { seen_url=url; return true; }
 void setTimeout(uint32_t) {} void setReuse(bool) {}
 void collectHeaders(const char**,int) {} void addHeader(const char*,const String&) {}
 int GET() { return 200; } String header(const char* key) { return String(headers[key]); }
 int getSize() { return declared; } WiFiClient* getStreamPtr() { static WiFiClient raw; return &raw; }
 void end() {}
};
''')
    header('esp_heap_caps.h', '#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 0\ninline void* heap_caps_malloc(size_t n,int) { return malloc(n); }\n')
    header('services/http_progress.h', r'''
#include <Stream.h>
extern bool progress_active;
extern int progress_reads;
struct HttpStallTime {};
inline bool httpProgressActive() { return progress_active; }
class HttpProgressStream : public Stream {
 Stream& raw;
public: explicit HttpProgressStream(Stream& s) : raw(s) {}
 size_t readBytes(char* p,size_t n) override { ++progress_reads; return raw.readBytes(p,n); }
 int available() override { return raw.available(); }
};
''')
    source = r'''
#include <assert.h>
#include <HTTPClient.h>
#include "services/filaman_labels.h"
std::map<std::string,std::string> headers;
std::string seen_url;
size_t bytes_left=0;
int declared=0, progress_reads=0;
bool progress_active=false;
int main() {
  headers={{"X-Image-Width","384"},{"X-Image-Height","240"},{"X-Row-Bytes","48"},
           {"X-Content-Width","320"},{"X-Rotated","0"},{"X-Bit-Order","msb-black-1"}};
  declared=48*240;
  LabelRaster image{};
  for (bool active: {false,true}) {
    progress_active=active; progress_reads=0; bytes_left=declared;
    assert(filamanFetchMonoLabel("http://fila","key",123,7,384,"landscape",&image)==200);
    assert(seen_url=="http://fila/api/v1/labels/spool/123/render?format=mono1&dpi=203&align=right&orientation=landscape&width=384&preset_id=7");
    assert(progress_reads==(active ? 1 : 0));
    filamanFreeLabel(&image);
    for (int delta: {-1,1}) {
      bytes_left=declared+delta;
      assert(filamanFetchMonoLabel("http://fila","key",123,7,384,"portrait",&image)==-2);
      assert(!image.pixels);
    }
  }
}
'''
    result = subprocess.run(['g++','-std=c++11',f'-I{d}',f'-I{root / "src"}','-x','c++','-',str(root/'src/services/filaman_labels.cpp'),'-o',str(d/'raster-check')],input=source,text=True,capture_output=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(d/'raster-check')],capture_output=True,text=True)
    assert result.returncode == 0, result.stderr
