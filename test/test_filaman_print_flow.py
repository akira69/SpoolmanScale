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
